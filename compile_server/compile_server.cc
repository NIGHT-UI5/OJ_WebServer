#include "../common/httplib.h"
#include "../common/file_util.hpp"
#include "../common/log.hpp"
#include "judge.hpp"

#include <jsoncpp/json/json.h>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string ToJsonString(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    return Json::writeString(builder, value);
}

bool ParseJson(const std::string& body, Json::Value* root, std::string* error) {
    Json::CharReaderBuilder builder;
    std::istringstream input(body);
    return Json::parseFromStream(builder, input, root, error);
}

bool GetRequiredString(const Json::Value& root,
                       const std::string& key,
                       std::string* value,
                       std::string* error) {
    if (!root.isMember(key) || !root[key].isString()) {
        *error = "missing or invalid string field: " + key;
        return false;
    }

    *value = root[key].asString();
    return true;
}

int GetOptionalInt(const Json::Value& root, const std::string& key, int default_value) {
    if (!root.isMember(key)) {
        return default_value;
    }

    if (!root[key].isInt()) {
        return default_value;
    }

    return root[key].asInt();
}

bool GetOptionalPositiveInt(const Json::Value& root,
                            const std::string& key,
                            int* value,
                            std::string* error) {
    if (!root.isMember(key)) {
        return true;
    }

    const Json::Value& field = root[key];
    if ((field.type() != Json::intValue && field.type() != Json::uintValue) ||
        !field.isInt() || field.asInt() <= 0) {
        *error = "invalid positive integer field: " + key;
        return false;
    }

    *value = root[key].asInt();
    return true;
}

std::string ReadTaskFile(const std::string& path) {
    std::string content;
    if (oj::FileUtil::Exists(path)) {
        oj::FileUtil::ReadFile(path, &content);
    }
    return content;
}

Json::Value ErrorResponse(const std::string& message) {
    Json::Value response;
    response["ok"] = false;
    response["error"] = message;
    return response;
}

Json::Value JudgeResponse(const oj::JudgeResult& result) {
    Json::Value response;
    response["ok"] = true;
    response["judge_status"] = oj::JudgeStatusName(result.status);
    response["run_status"] = oj::RunStatusName(result.run_result.status);
    response["exit_code"] = result.run_result.exit_code;
    response["signal"] = result.run_result.signal;
    return response;
}

bool BuildJudgeRequest(const Json::Value& root,
                       oj::JudgeRequest* request,
                       std::string* error) {
    return GetRequiredString(root, "src_path", &request->src_path, error) &&
           GetRequiredString(root, "exe_path", &request->exe_path, error) &&
           GetRequiredString(root, "input_path", &request->input_path, error) &&
           GetRequiredString(root, "output_path", &request->output_path, error) &&
           GetRequiredString(root, "answer_path", &request->answer_path, error) &&
           GetRequiredString(root, "compile_err_path", &request->compile_err_path, error) &&
           GetRequiredString(root, "runtime_err_path", &request->runtime_err_path, error);
}

}  // namespace

int main(int argc, char* argv[]) {
    int port = 8081;
    if (argc >= 2) {
        port = std::atoi(argv[1]);
    }

    httplib::Server server;
    // Permit immediate restart without allowing multiple listeners on one port.
    server.set_socket_options([](socket_t socket) {
        httplib::set_socket_opt(socket, SOL_SOCKET, SO_REUSEADDR, 1);
    });

    server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        Json::Value response;
        response["ok"] = true;
        response["service"] = "compile_server";
        res.set_content(ToJsonString(response), "application/json");
    });

    server.Post("/judge", [](const httplib::Request& req, httplib::Response& res) {
        Json::Value root;
        std::string error;
        if (!ParseJson(req.body, &root, &error)) {
            LOG(WARNING) << "parse judge request json failed: " << error;
            res.status = 400;
            res.set_content(ToJsonString(ErrorResponse("invalid json: " + error)),
                            "application/json");
            return;
        }

        oj::JudgeRequest judge_request;
        if (!BuildJudgeRequest(root, &judge_request, &error)) {
            LOG(WARNING) << "invalid judge request: " << error;
            res.status = 400;
            res.set_content(ToJsonString(ErrorResponse(error)), "application/json");
            return;
        }

        judge_request.cpu_limit_sec = GetOptionalInt(root, "cpu_limit_sec", 1);
        judge_request.mem_limit_mb = GetOptionalInt(root, "mem_limit_mb", 64);
        if (!GetOptionalPositiveInt(root, "wall_limit_sec", &judge_request.wall_limit_sec, &error) ||
            !GetOptionalPositiveInt(root, "output_limit_mb", &judge_request.output_limit_mb, &error)) {
            res.status = 400;
            res.set_content(ToJsonString(ErrorResponse(error)), "application/json");
            return;
        }

        LOG(INFO) << "receive judge request, src: " << judge_request.src_path;
        oj::JudgeResult judge_result = oj::Judge::Run(judge_request);
        res.set_content(ToJsonString(JudgeResponse(judge_result)), "application/json");
    });

    server.Post("/judge_code", [](const httplib::Request& req, httplib::Response& res) {
        Json::Value root;
        std::string error;
        if (!ParseJson(req.body, &root, &error)) {
            res.status = 400;
            res.set_content(ToJsonString(ErrorResponse("invalid json: " + error)),
                            "application/json");
            return;
        }

        if (!root.isObject()) {
            res.status = 400;
            res.set_content(ToJsonString(ErrorResponse("json body must be an object")),
                            "application/json");
            return;
        }

        std::string code;
        std::string input;
        std::string answer;
        oj::JudgeRequest judge_request;
        if (!GetRequiredString(root, "code", &code, &error) ||
            !GetRequiredString(root, "input", &input, &error) ||
            !GetRequiredString(root, "answer", &answer, &error) ||
            !GetOptionalPositiveInt(root, "cpu_limit_sec", &judge_request.cpu_limit_sec, &error) ||
            !GetOptionalPositiveInt(root, "mem_limit_mb", &judge_request.mem_limit_mb, &error) ||
            !GetOptionalPositiveInt(root, "wall_limit_sec", &judge_request.wall_limit_sec, &error) ||
            !GetOptionalPositiveInt(root, "output_limit_mb", &judge_request.output_limit_mb, &error)) {
            res.status = 400;
            res.set_content(ToJsonString(ErrorResponse(error)), "application/json");
            return;
        }

        if (code.empty()) {
            res.status = 400;
            res.set_content(ToJsonString(ErrorResponse("code must not be empty")),
                            "application/json");
            return;
        }

        std::string task_id;
        std::string task_dir;
        if (!oj::FileUtil::CreateTaskDir("tmp", &task_id, &task_dir)) {
            res.status = 500;
            res.set_content(ToJsonString(ErrorResponse("create task directory failed")),
                            "application/json");
            return;
        }

        judge_request.src_path = oj::FileUtil::JoinPath(task_dir, "main.cc");
        judge_request.exe_path = oj::FileUtil::JoinPath(task_dir, "main");
        judge_request.input_path = oj::FileUtil::JoinPath(task_dir, "input.txt");
        judge_request.output_path = oj::FileUtil::JoinPath(task_dir, "output.txt");
        judge_request.answer_path = oj::FileUtil::JoinPath(task_dir, "answer.txt");
        judge_request.compile_err_path = oj::FileUtil::JoinPath(task_dir, "compile.err");
        judge_request.runtime_err_path = oj::FileUtil::JoinPath(task_dir, "runtime.err");

        if (!oj::FileUtil::WriteFile(judge_request.src_path, code) ||
            !oj::FileUtil::WriteFile(judge_request.input_path, input) ||
            !oj::FileUtil::WriteFile(judge_request.answer_path, answer)) {
            LOG(ERROR) << "write judge code files failed, task: " << task_id;
            res.status = 500;
            res.set_content(ToJsonString(ErrorResponse("write task files failed")),
                            "application/json");
            return;
        }

        LOG(INFO) << "receive judge code request, task: " << task_id;
        oj::JudgeResult judge_result = oj::Judge::Run(judge_request);
        Json::Value response = JudgeResponse(judge_result);
        response["task_id"] = task_id;
        response["output"] = ReadTaskFile(judge_request.output_path);
        response["compile_error"] = ReadTaskFile(judge_request.compile_err_path);
        response["runtime_error"] = ReadTaskFile(judge_request.runtime_err_path);
        res.set_content(ToJsonString(response), "application/json");
    });

    LOG(INFO) << "compile server listen on port: " << port;
    if (!server.listen("0.0.0.0", port)) {
        LOG(ERROR) << "compile server listen failed, port: " << port;
        return 1;
    }

    return 0;
}
