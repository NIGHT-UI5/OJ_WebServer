#pragma once

#include <jsoncpp/json/json.h>
#include <sys/stat.h>

#include <exception>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace oj {

struct ProblemLimits {
    int cpu_limit_sec;
    int mem_limit_mb;
    int wall_limit_sec;
    int output_limit_mb;
};

struct ProblemSample {
    std::string input;
    std::string output;
};

struct Problem {
    std::string id;
    std::string title;
    std::string difficulty;
    std::string description;
    std::string starter_code;
    ProblemLimits limits;
    std::vector<ProblemSample> samples;
    // Server-only data, never included in the public query responses.
    std::string test_input;
    std::string test_answer;
};

class ProblemModel {
public:
    static bool ValidId(const std::string& id) {
        if (id.empty() || id[0] < '1' || id[0] > '9') {
            return false;
        }
        for (char digit : id) {
            if (digit < '0' || digit > '9') {
                return false;
            }
        }
        return true;
    }

    // Load once before starting HTTP workers. A failed load preserves the old catalog.
    bool Load(const std::string& path, std::string* error) {
        std::string text;
        if (!ReadFile(path, &text, error)) {
            return false;
        }
        Json::CharReaderBuilder reader;
        reader["failIfExtra"] = true;
        reader["rejectDupKeys"] = true;
        Json::Value root;
        std::istringstream input(text);
        try {
            if (!Json::parseFromStream(reader, input, &root, error)) {
                *error = "invalid problem JSON: " + *error;
                return false;
            }
        } catch (const std::exception& exception) {
            *error = std::string("invalid problem JSON: ") + exception.what();
            return false;
        }
        if (!root.isObject() || !root["problems"].isArray()) {
            *error = "problem catalog must be an object with a problems array";
            return false;
        }
        const std::size_t slash = path.find_last_of('/');
        const std::string directory = slash == std::string::npos ? "." : path.substr(0, slash);
        std::vector<Problem> problems;
        std::map<std::string, std::size_t> index;
        for (Json::ArrayIndex i = 0; i < root["problems"].size(); ++i) {
            Problem problem;
            std::string detail;
            if (!ParseProblem(root["problems"][i], &problem, &detail)) {
                *error = "invalid problem at index " + std::to_string(i) + ": " + detail;
                return false;
            }
            if (index.count(problem.id)) {
                *error = "duplicate problem id: " + problem.id;
                return false;
            }
            const std::string test_dir = directory + "/test_cases/" + problem.id;
            if (!ReadFile(test_dir + "/input.txt", &problem.test_input, error) ||
                !ReadFile(test_dir + "/answer.txt", &problem.test_answer, error)) {
                return false;
            }
            index[problem.id] = problems.size();
            problems.push_back(problem);
        }
        problems_.swap(problems);
        index_.swap(index);
        return true;
    }

    const std::vector<Problem>& AllProblems() const { return problems_; }

    const Problem* Find(const std::string& id) const {
        const auto found = index_.find(id);
        return found == index_.end() ? nullptr : &problems_[found->second];
    }

private:
    static bool ReadFile(const std::string& path, std::string* content, std::string* error) {
        struct stat info;
        if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode)) {
            *error = "problem data is not a readable regular file: " + path;
            return false;
        }
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            *error = "cannot open problem data: " + path;
            return false;
        }
        std::ostringstream buffer;
        char chunk[4096];
        while (file.read(chunk, sizeof(chunk)) || file.gcount() != 0) {
            buffer.write(chunk, file.gcount());
        }
        if (file.bad() || !file.eof()) {
            *error = "cannot read problem data: " + path;
            return false;
        }
        *content = buffer.str();
        return true;
    }

    static bool PositiveInt(const Json::Value& value) {
        return (value.type() == Json::intValue || value.type() == Json::uintValue) &&
               value.isInt() && value.asInt() > 0;
    }

    static bool ParseProblem(const Json::Value& value, Problem* problem, std::string* error) {
        if (!value.isObject()) {
            *error = "expected an object";
            return false;
        }
        for (const char* key : {"id", "title", "difficulty", "description", "starter_code"}) {
            if (!value[key].isString() || value[key].asString().empty()) {
                *error = std::string("missing or empty string field: ") + key;
                return false;
            }
        }
        problem->id = value["id"].asString();
        problem->title = value["title"].asString();
        problem->difficulty = value["difficulty"].asString();
        problem->description = value["description"].asString();
        problem->starter_code = value["starter_code"].asString();
        if (!ValidId(problem->id)) {
            *error = "id must be a positive decimal string without leading zeroes";
            return false;
        }
        if (problem->difficulty != "easy" && problem->difficulty != "medium" && problem->difficulty != "hard") {
            *error = "difficulty must be easy, medium or hard";
            return false;
        }
        const Json::Value& limits = value["limits"];
        if (!limits.isObject()) {
            *error = "missing or invalid limits object";
            return false;
        }
        for (const char* key : {"cpu_limit_sec", "mem_limit_mb", "wall_limit_sec", "output_limit_mb"}) {
            if (!PositiveInt(limits[key])) {
                *error = std::string("invalid positive integer limit: ") + key;
                return false;
            }
        }
        problem->limits = {limits["cpu_limit_sec"].asInt(), limits["mem_limit_mb"].asInt(),
                           limits["wall_limit_sec"].asInt(), limits["output_limit_mb"].asInt()};
        if (!value["samples"].isArray()) {
            *error = "missing or invalid samples array";
            return false;
        }
        for (const auto& sample : value["samples"]) {
            if (!sample.isObject() || !sample["input"].isString() || !sample["output"].isString()) {
                *error = "sample must contain input and output strings";
                return false;
            }
            problem->samples.push_back({sample["input"].asString(), sample["output"].asString()});
        }
        return true;
    }

    std::vector<Problem> problems_;
    std::map<std::string, std::size_t> index_;
};

}  // namespace oj
