#pragma once

#include <fstream>
#include <sstream>
#include <string>

#include "../common/log.hpp"
#include "compiler.hpp"
#include "runner.hpp"

namespace oj {

enum class JudgeStatus {
    ACCEPTED,
    WRONG_ANSWER,
    COMPILE_ERROR,
    RUNTIME_ERROR,
    TIME_LIMIT_EXCEEDED,
    OUTPUT_LIMIT_EXCEEDED,
    SYSTEM_ERROR
};

inline const char* JudgeStatusName(JudgeStatus status) {
    switch (status) {
    case JudgeStatus::ACCEPTED:
        return "ACCEPTED";
    case JudgeStatus::WRONG_ANSWER:
        return "WRONG_ANSWER";
    case JudgeStatus::COMPILE_ERROR:
        return "COMPILE_ERROR";
    case JudgeStatus::RUNTIME_ERROR:
        return "RUNTIME_ERROR";
    case JudgeStatus::TIME_LIMIT_EXCEEDED:
        return "TIME_LIMIT_EXCEEDED";
    case JudgeStatus::OUTPUT_LIMIT_EXCEEDED:
        return "OUTPUT_LIMIT_EXCEEDED";
    case JudgeStatus::SYSTEM_ERROR:
        return "SYSTEM_ERROR";
    default:
        return "UNKNOWN";
    }
}

struct JudgeResult {
    JudgeStatus status;
    RunResult run_result;

    JudgeResult(JudgeStatus s = JudgeStatus::SYSTEM_ERROR,
                const RunResult& r = RunResult())
        : status(s), run_result(r) {}
};

struct JudgeRequest {
    std::string src_path;
    std::string exe_path;
    std::string input_path;
    std::string output_path;
    std::string answer_path;
    std::string compile_err_path;
    std::string runtime_err_path;
    int cpu_limit_sec;
    int mem_limit_mb;
    int wall_limit_sec;
    int output_limit_mb;

    JudgeRequest()
        : cpu_limit_sec(1), mem_limit_mb(64), wall_limit_sec(3), output_limit_mb(1) {}
};

class Judge {
public:
    static JudgeResult Run(const JudgeRequest& request) {
        if (!Compiler::Compile(request.src_path, request.exe_path, request.compile_err_path)) {
            LOG(WARNING) << "judge result: compile error, src: " << request.src_path;
            return JudgeResult(JudgeStatus::COMPILE_ERROR);
        }

        RunResult run_result = Runner::Run(request.exe_path,
                                           request.input_path,
                                           request.output_path,
                                           request.runtime_err_path,
                                           request.cpu_limit_sec,
                                           request.mem_limit_mb,
                                           request.wall_limit_sec,
                                           request.output_limit_mb);
        if (run_result.status == RunStatus::OUTPUT_LIMIT_EXCEEDED) {
            LOG(WARNING) << "judge result: output limit exceeded, src: " << request.src_path;
            return JudgeResult(JudgeStatus::OUTPUT_LIMIT_EXCEEDED, run_result);
        }
        if (run_result.status == RunStatus::TIME_LIMIT_EXCEEDED) {
            LOG(WARNING) << "judge result: time limit exceeded, src: " << request.src_path;
            return JudgeResult(JudgeStatus::TIME_LIMIT_EXCEEDED, run_result);
        }

        if (run_result.status == RunStatus::RUNTIME_ERROR) {
            LOG(WARNING) << "judge result: runtime error, src: " << request.src_path;
            return JudgeResult(JudgeStatus::RUNTIME_ERROR, run_result);
        }

        if (run_result.status == RunStatus::SYSTEM_ERROR) {
            LOG(ERROR) << "judge result: system error, src: " << request.src_path;
            return JudgeResult(JudgeStatus::SYSTEM_ERROR, run_result);
        }

        std::string user_output;
        std::string standard_answer;
        if (!ReadFile(request.output_path, &user_output)) {
            LOG(ERROR) << "read user output failed: " << request.output_path;
            return JudgeResult(JudgeStatus::SYSTEM_ERROR, run_result);
        }

        if (!ReadFile(request.answer_path, &standard_answer)) {
            LOG(ERROR) << "read answer file failed: " << request.answer_path;
            return JudgeResult(JudgeStatus::SYSTEM_ERROR, run_result);
        }

        if (user_output == standard_answer) {
            LOG(INFO) << "judge result: accepted, src: " << request.src_path;
            return JudgeResult(JudgeStatus::ACCEPTED, run_result);
        }

        LOG(WARNING) << "judge result: wrong answer, src: " << request.src_path;
        return JudgeResult(JudgeStatus::WRONG_ANSWER, run_result);
    }

    static JudgeResult Run(const std::string& src_path,
                           const std::string& exe_path,
                           const std::string& input_path,
                           const std::string& output_path,
                           const std::string& answer_path,
                           const std::string& compile_err_path,
                           const std::string& runtime_err_path,
                           int cpu_limit_sec = 1,
                           int mem_limit_mb = 64,
                           int wall_limit_sec = 3,
                           int output_limit_mb = 1) {
        JudgeRequest request;
        request.src_path = src_path;
        request.exe_path = exe_path;
        request.input_path = input_path;
        request.output_path = output_path;
        request.answer_path = answer_path;
        request.compile_err_path = compile_err_path;
        request.runtime_err_path = runtime_err_path;
        request.cpu_limit_sec = cpu_limit_sec;
        request.mem_limit_mb = mem_limit_mb;
        request.wall_limit_sec = wall_limit_sec;
        request.output_limit_mb = output_limit_mb;
        return Run(request);
    }

private:
    static bool ReadFile(const std::string& path, std::string* content) {
        std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        std::ostringstream buffer;
        buffer << file.rdbuf();
        *content = buffer.str();
        return true;
    }
};

}  // namespace oj
