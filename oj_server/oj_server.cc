#include "../common/httplib.h"
#include "../common/log.hpp"
#include "load_balancer.hpp"
#include "node_health_checker.hpp"
#include "problem_model.hpp"

#include <jsoncpp/json/json.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string ToJsonString(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

bool ParseJson(const std::string& body, Json::Value* root, std::string* error) {
    Json::CharReaderBuilder builder;
    builder["failIfExtra"] = true;
    builder["rejectDupKeys"] = true;
    std::istringstream input(body);
    try {
        return Json::parseFromStream(builder, input, root, error);
    } catch (const std::exception& exception) {
        *error = exception.what();
        return false;
    }
}

void SetError(httplib::Response& response, int status, const std::string& message) {
    Json::Value body;
    body["ok"] = false;
    body["error"] = message;
    response.status = status;
    response.set_content(ToJsonString(body), "application/json");
}

bool ValidateSubmission(const Json::Value& root, std::string* error) {
    if (!root.isObject()) {
        *error = "json body must be an object";
        return false;
    }
    for (const char* key : {"problem_id", "code"}) {
        if (!root.isMember(key) || !root[key].isString()) {
            *error = std::string("missing or invalid string field: ") + key;
            return false;
        }
    }
    if (!oj::ProblemModel::ValidId(root["problem_id"].asString())) {
        *error = "invalid problem id";
        return false;
    }
    if (root["code"].asString().empty()) {
        *error = "code must not be empty";
        return false;
    }
    for (const auto& key : root.getMemberNames()) {
        if (key != "problem_id" && key != "code") {
            *error = "unsupported submission field: " + key;
            return false;
        }
    }
    return true;
}

bool ParsePositiveInt(const char* text, int maximum, int* value) {
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed <= 0 || parsed > maximum) {
        return false;
    }
    *value = static_cast<int>(parsed);
    return true;
}

bool ReadNodes(const std::string& path, std::vector<oj::CompileNode>* nodes,
               std::string* error) {
    std::ifstream file(path);
    if (!file.is_open()) {
        *error = "cannot open node configuration: " + path;
        return false;
    }
    std::string line;
    int line_number = 0;
    while (std::getline(file, line)) {
        ++line_number;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        std::istringstream fields(line);
        oj::CompileNode node;
        std::string port, extra;
        if (!(fields >> node.host)) {
            continue;
        }
        if (!(fields >> port) || (fields >> extra) ||
            !ParsePositiveInt(port.c_str(), 65535, &node.port)) {
            *error = "invalid node at " + path + ":" + std::to_string(line_number) +
                     " (expected: host port; port: 1..65535)";
            return false;
        }
        nodes->push_back(node);
    }
    if (file.bad()) {
        *error = "cannot read node configuration: " + path;
        return false;
    }
    if (nodes->empty()) {
        *error = "node configuration is empty: " + path;
        return false;
    }
    return true;
}

bool ExecutableDirectory(std::string* directory) {
    // Resolve from the real executable so launching from another directory also works.
    char executable[4096];
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable));
    if (length <= 0 || static_cast<std::size_t>(length) >= sizeof(executable)) {
        return false;
    }
    const std::string binary(executable, static_cast<std::size_t>(length));
    *directory = binary.substr(0, binary.find_last_of('/'));
    return true;
}

void RegisterWebPages(httplib::Server& server, const std::string& directory) {
    struct Page {
        const char* route;
        const char* file;
        const char* type;
    };
    // Only these public files are served. The project and test-data directories are not mounted.
    const Page pages[] = {
        {"/", "index.html", "text/html; charset=utf-8"},
        {R"(/index\.html)", "index.html", "text/html; charset=utf-8"},
        {R"(/problem\.html)", "problem.html", "text/html; charset=utf-8"},
        {R"(/assets/styles\.css)", "assets/styles.css", "text/css; charset=utf-8"},
        {R"(/assets/favicon\.svg)", "assets/favicon.svg", "image/svg+xml"},
        {R"(/assets/common\.js)", "assets/common.js", "text/javascript; charset=utf-8"},
        {R"(/assets/problems\.js)", "assets/problems.js", "text/javascript; charset=utf-8"},
        {R"(/assets/problem\.js)", "assets/problem.js", "text/javascript; charset=utf-8"},
    };
    for (const auto& page : pages) {
        const std::string path = directory + "/" + page.file;
        const std::string type = page.type;
        server.Get(page.route, [path, type](const httplib::Request&, httplib::Response& response) {
            response.set_header("Cache-Control", "no-cache");
            response.set_header("X-Content-Type-Options", "nosniff");
            response.set_file_content(path, type);
        });
    }
}

Json::Value ProblemSummary(const oj::Problem& problem) {
    Json::Value value;
    value["id"] = problem.id;
    value["title"] = problem.title;
    value["difficulty"] = problem.difficulty;
    return value;
}

Json::Value ProblemDetail(const oj::Problem& problem) {
    Json::Value value = ProblemSummary(problem);
    value["description"] = problem.description;
    value["starter_code"] = problem.starter_code;
    value["limits"]["cpu_limit_sec"] = problem.limits.cpu_limit_sec;
    value["limits"]["mem_limit_mb"] = problem.limits.mem_limit_mb;
    value["limits"]["wall_limit_sec"] = problem.limits.wall_limit_sec;
    value["limits"]["output_limit_mb"] = problem.limits.output_limit_mb;
    value["samples"] = Json::Value(Json::arrayValue);
    for (const auto& sample : problem.samples) {
        Json::Value item;
        item["input"] = sample.input;
        item["output"] = sample.output;
        value["samples"].append(item);
    }
    return value;
}

void Usage(const char* program) {
    std::cerr << "Usage: " << program
              << " [listen_port] [compile_host] [compile_port] [timeout_sec] [--problems <file>]\n"
              << "   or: " << program << " --nodes <config_file> [listen_port] [timeout_sec] [--problems <file>]\n"
              << "Defaults: 8080 127.0.0.1 8081 30; ports: 1..65535; timeout: positive integer\n"
              << "Default problem file: ../data/problems.json relative to executable directory\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    int listen_port = 8080;
    int timeout_sec = 30;
    std::string problem_file;
    std::vector<std::string> arguments;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--problems") {
            if (!problem_file.empty() || i + 1 >= argc || std::string(argv[i + 1]).empty()) {
                Usage(argv[0]);
                return 1;
            }
            problem_file = argv[++i];
        } else {
            arguments.push_back(argv[i]);
        }
    }
    std::vector<oj::CompileNode> nodes;
    if (!arguments.empty() && arguments[0] == "--nodes") {
        if (arguments.size() < 2 || arguments.size() > 4 ||
            (arguments.size() >= 3 && !ParsePositiveInt(arguments[2].c_str(), 65535, &listen_port)) ||
            (arguments.size() >= 4 && !ParsePositiveInt(arguments[3].c_str(), INT_MAX, &timeout_sec))) {
            Usage(argv[0]);
            return 1;
        }
        std::string error;
        if (!ReadNodes(arguments[1], &nodes, &error)) {
            LOG(ERROR) << error;
            return 1;
        }
    } else {
        oj::CompileNode node = {arguments.size() >= 2 ? arguments[1] : "127.0.0.1", 8081};
        if (arguments.size() > 4 || node.host.empty() ||
            (!arguments.empty() && !ParsePositiveInt(arguments[0].c_str(), 65535, &listen_port)) ||
            (arguments.size() >= 3 && !ParsePositiveInt(arguments[2].c_str(), 65535, &node.port)) ||
            (arguments.size() >= 4 && !ParsePositiveInt(arguments[3].c_str(), INT_MAX, &timeout_sec))) {
            Usage(argv[0]);
            return 1;
        }
        nodes.push_back(node);
    }

    std::string executable_dir;
    if (!ExecutableDirectory(&executable_dir)) {
        LOG(ERROR) << "cannot resolve executable directory";
        return 1;
    }
    if (problem_file.empty()) {
        problem_file = executable_dir + "/../data/problems.json";
    }
    oj::ProblemModel problem_model;
    std::string problem_error;
    if (!problem_model.Load(problem_file, &problem_error)) {
        LOG(ERROR) << "load problem catalog failed: " << problem_error;
        return 1;
    }
    LOG(INFO) << "loaded problem catalog: " << problem_file
              << ", problem count: " << problem_model.AllProblems().size();

    oj::LoadBalancer load_balancer(nodes);
    httplib::Server server;
    // Permit immediate restart without allowing multiple listeners on one port.
    server.set_socket_options([](socket_t socket) {
        httplib::set_socket_opt(socket, SOL_SOCKET, SO_REUSEADDR, 1);
    });
    RegisterWebPages(server, executable_dir + "/../web");
    server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
        Json::Value body;
        body["ok"] = true;
        body["service"] = "oj_server";
        response.set_content(ToJsonString(body), "application/json");
    });

    server.Get("/problems", [&problem_model](const httplib::Request&, httplib::Response& response) {
        Json::Value body;
        body["ok"] = true;
        body["total"] = static_cast<Json::UInt64>(problem_model.AllProblems().size());
        body["problems"] = Json::Value(Json::arrayValue);
        for (const auto& problem : problem_model.AllProblems()) {
            body["problems"].append(ProblemSummary(problem));
        }
        response.set_content(ToJsonString(body), "application/json");
    });

    server.Get(R"(/problems/(.*))", [&problem_model](const httplib::Request& request, httplib::Response& response) {
        const std::string id = request.matches[1].str();
        if (!oj::ProblemModel::ValidId(id)) {
            SetError(response, 400, "invalid problem id");
            return;
        }
        const oj::Problem* problem = problem_model.Find(id);
        if (problem == nullptr) {
            SetError(response, 404, "problem not found");
            return;
        }
        Json::Value body;
        body["ok"] = true;
        body["problem"] = ProblemDetail(*problem);
        response.set_content(ToJsonString(body), "application/json");
    });

    server.Post("/submit", [&load_balancer, &problem_model, timeout_sec](
                             const httplib::Request& request, httplib::Response& response) {
        Json::Value submission;
        std::string error;
        if (!ParseJson(request.body, &submission, &error)) {
            SetError(response, 400, "invalid json: " + error);
            return;
        }
        if (!ValidateSubmission(submission, &error)) {
            SetError(response, 400, error);
            return;
        }
        const oj::Problem* problem = problem_model.Find(submission["problem_id"].asString());
        if (problem == nullptr) {
            SetError(response, 404, "problem not found");
            return;
        }

        // Only source code comes from the caller. Tests and limits belong to the catalog.
        Json::Value judge_request;
        judge_request["code"] = submission["code"];
        judge_request["input"] = problem->test_input;
        judge_request["answer"] = problem->test_answer;
        judge_request["cpu_limit_sec"] = problem->limits.cpu_limit_sec;
        judge_request["mem_limit_mb"] = problem->limits.mem_limit_mb;
        judge_request["wall_limit_sec"] = problem->limits.wall_limit_sec;
        judge_request["output_limit_mb"] = problem->limits.output_limit_mb;

        oj::LoadBalancer::RequestGuard node_request(load_balancer);
        if (!node_request.Valid()) {
            SetError(response, 503, "no available compile server");
            return;
        }
        const oj::CompileNode& node = node_request.Node();

        // The guard releases the count on every return, including errors and timeouts.
        // Selection releases its lock before any network or judging work begins.
        // Each request owns its client so concurrent submissions do not share a connection.
        httplib::Client client(node.host, node.port);
        client.set_connection_timeout(std::min(3, timeout_sec), 0);
        client.set_read_timeout(timeout_sec, 0);
        client.set_write_timeout(timeout_sec, 0);
        client.set_max_timeout(std::chrono::seconds(timeout_sec));

        LOG(INFO) << "forward problem " << problem->id << " to compile server: " << node.host << ":" << node.port;
        const auto start = std::chrono::steady_clock::now();
        auto result = client.Post("/judge_code", ToJsonString(judge_request), "application/json");
        if (!result) {
            const auto elapsed = std::chrono::steady_clock::now() - start;
            // This httplib version may report a read/write timeout as a generic I/O error.
            const bool io_deadline = (result.error() == httplib::Error::Read ||
                                      result.error() == httplib::Error::Write) &&
                elapsed + std::chrono::milliseconds(10) >= std::chrono::seconds(timeout_sec);
            const bool timed_out = result.error() == httplib::Error::ConnectionTimeout ||
                                   result.error() == httplib::Error::Timeout || io_deadline;
            LOG(ERROR) << "compile server request failed: " << httplib::to_string(result.error());
            if (node_request.MarkUnavailable()) {
                LOG(WARNING) << "compile server marked unavailable: " << node.host << ":" << node.port;
            }
            SetError(response, timed_out ? 504 : 502,
                     timed_out ? "compile server request timed out" : "compile server unavailable");
            return;
        }

        Json::Value body;
        if (!ParseJson(result->body, &body, &error) || !body.isObject() || !body["ok"].isBool()) {
            LOG(ERROR) << "invalid response from compile server, HTTP status: " << result->status;
            if (node_request.MarkUnavailable()) {
                LOG(WARNING) << "compile server marked unavailable: " << node.host << ":" << node.port;
            }
            SetError(response, 502, "invalid response from compile server");
            return;
        }

        // Retain the node's result fields and HTTP status, and identify the submitted problem.
        body["problem_id"] = problem->id;
        response.status = result->status;
        response.set_content(ToJsonString(body), "application/json");
    });

    LOG(INFO) << "OJ server listen on port: " << listen_port
              << ", compile server count: " << nodes.size();
    for (const auto& node : nodes) {
        LOG(INFO) << "compile server node: " << node.host << ":" << node.port;
    }
    oj::NodeHealthChecker health_checker(load_balancer);
    if (!server.listen("0.0.0.0", listen_port)) {
        LOG(ERROR) << "OJ server listen failed, port: " << listen_port;
        return 1;
    }
    return 0;
}
