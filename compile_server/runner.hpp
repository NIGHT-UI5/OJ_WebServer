#pragma once

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <string>

#include "../common/log.hpp"

namespace oj {

enum class RunStatus {
    OK,
    RUNTIME_ERROR,
    TIME_LIMIT_EXCEEDED,
    OUTPUT_LIMIT_EXCEEDED,
    SYSTEM_ERROR
};

inline const char* RunStatusName(RunStatus status) {
    switch (status) {
    case RunStatus::OK:
        return "OK";
    case RunStatus::RUNTIME_ERROR:
        return "RUNTIME_ERROR";
    case RunStatus::TIME_LIMIT_EXCEEDED:
        return "TIME_LIMIT_EXCEEDED";
    case RunStatus::OUTPUT_LIMIT_EXCEEDED:
        return "OUTPUT_LIMIT_EXCEEDED";
    case RunStatus::SYSTEM_ERROR:
        return "SYSTEM_ERROR";
    default:
        return "UNKNOWN";
    }
}

struct RunResult {
    RunStatus status;
    int exit_code;
    int signal;

    RunResult(RunStatus s = RunStatus::SYSTEM_ERROR, int e = -1, int sig = 0)
        : status(s), exit_code(e), signal(sig) {}
};

class Runner {
public:
    static RunResult Run(const std::string& exe_path,
                         const std::string& input_path,
                         const std::string& output_path,
                         const std::string& err_path,
                         int cpu_limit_sec = 1,
                         int mem_limit_mb = 64,
                         int wall_limit_sec = 3,
                         int output_limit_mb = 1) {
        if (wall_limit_sec <= 0) {
            LOG(ERROR) << "invalid wall time limit: " << wall_limit_sec;
            return RunResult(RunStatus::SYSTEM_ERROR);
        }
        if (output_limit_mb <= 0) {
            LOG(ERROR) << "invalid output file size limit: " << output_limit_mb;
            return RunResult(RunStatus::SYSTEM_ERROR);
        }

        // A monotonic clock keeps elapsed time independent of system clock changes.
        const auto start = std::chrono::steady_clock::now();
        pid_t pid = fork();
        if (pid < 0) {
            LOG(ERROR) << "fork failed while running: " << exe_path;
            return RunResult(RunStatus::SYSTEM_ERROR);
        }

        if (pid == 0) {
            int input_fd = open(input_path.c_str(), O_RDONLY);
            if (input_fd < 0) {
                LOG(ERROR) << "open input file failed: " << input_path;
                _exit(2);
            }

            int output_fd = open(output_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (output_fd < 0) {
                LOG(ERROR) << "open output file failed: " << output_path;
                close(input_fd);
                _exit(3);
            }

            int err_fd = open(err_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (err_fd < 0) {
                LOG(ERROR) << "open runtime error file failed: " << err_path;
                close(input_fd);
                close(output_fd);
                _exit(4);
            }

            dup2(input_fd, STDIN_FILENO);
            dup2(output_fd, STDOUT_FILENO);
            dup2(err_fd, STDERR_FILENO);

            close(input_fd);
            close(output_fd);
            close(err_fd);

            struct rlimit cpu_limit;
            cpu_limit.rlim_cur = cpu_limit_sec;
            cpu_limit.rlim_max = cpu_limit_sec;
            if (setrlimit(RLIMIT_CPU, &cpu_limit) < 0) {
                LOG(ERROR) << "set cpu limit failed, limit: " << cpu_limit_sec
                           << "s, exe: " << exe_path;
                _exit(6);
            }

            struct rlimit mem_limit;
            mem_limit.rlim_cur = static_cast<rlim_t>(mem_limit_mb) * 1024 * 1024;
            mem_limit.rlim_max = static_cast<rlim_t>(mem_limit_mb) * 1024 * 1024;
            if (setrlimit(RLIMIT_AS, &mem_limit) < 0) {
                LOG(ERROR) << "set memory limit failed, limit: " << mem_limit_mb
                           << "MB, exe: " << exe_path;
                _exit(7);
            }

            // RLIMIT_FSIZE applies separately to each regular file, including stdout/stderr.
            struct rlimit file_limit;
            file_limit.rlim_cur = static_cast<rlim_t>(output_limit_mb) * 1024 * 1024;
            file_limit.rlim_max = file_limit.rlim_cur;
            if (setrlimit(RLIMIT_FSIZE, &file_limit) < 0) {
                LOG(ERROR) << "set output file size limit failed, limit: " << output_limit_mb
                           << "MiB, exe: " << exe_path;
                _exit(8);
            }

            execl(exe_path.c_str(), exe_path.c_str(), nullptr);
            LOG(ERROR) << "exec program failed: " << exe_path;
            _exit(5);
        }

        int status = 0;
        bool wall_timeout = false;
        while (true) {
            pid_t result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                break;
            }
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                LOG(ERROR) << "waitpid failed while running: " << exe_path;
                return RunResult(RunStatus::SYSTEM_ERROR);
            }

            if (std::chrono::steady_clock::now() - start >=
                std::chrono::seconds(wall_limit_sec)) {
                // The child may exit between waitpid and kill; still reap its actual status.
                if (kill(pid, SIGKILL) < 0 && errno != ESRCH) {
                    LOG(ERROR) << "kill timed out program failed: " << exe_path;
                    return RunResult(RunStatus::SYSTEM_ERROR);
                }
                do {
                    result = waitpid(pid, &status, 0);
                } while (result < 0 && errno == EINTR);
                if (result < 0) {
                    LOG(ERROR) << "waitpid failed after timeout: " << exe_path;
                    return RunResult(RunStatus::SYSTEM_ERROR);
                }
                wall_timeout = WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL;
                break;
            }

            usleep(10000);  // Avoid busy waiting while the child is still running.
        }

        if (wall_timeout) {
            LOG(WARNING) << "program wall time limit exceeded, exe: " << exe_path
                         << ", wall time limit: " << wall_limit_sec << "s";
            return RunResult(RunStatus::TIME_LIMIT_EXCEEDED, 128 + SIGKILL, SIGKILL);
        }

        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 0) {
                LOG(INFO) << "program exited normally: " << exe_path;
                return RunResult(RunStatus::OK, exit_code, 0);
            } else {
                LOG(WARNING) << "program exited with code " << exit_code
                             << ", exe: " << exe_path;
                return RunResult(RunStatus::RUNTIME_ERROR, exit_code, 0);
            }
        }

        if (WIFSIGNALED(status)) {
            int signal = WTERMSIG(status);
            if (signal == SIGXFSZ) {
                LOG(WARNING) << "program output limit exceeded, exe: " << exe_path
                             << ", file size limit: " << output_limit_mb << "MiB";
                return RunResult(RunStatus::OUTPUT_LIMIT_EXCEEDED, 128 + signal, signal);
            }
            if (signal == SIGXCPU || signal == SIGKILL) {
                LOG(WARNING) << "program time limit exceeded, signal: " << signal
                             << ", exe: " << exe_path
                             << ", cpu limit: " << cpu_limit_sec << "s"
                             << ", memory limit: " << mem_limit_mb << "MB";
                return RunResult(RunStatus::TIME_LIMIT_EXCEEDED, 128 + signal, signal);
            }

            LOG(WARNING) << "program killed by signal " << signal
                         << ", exe: " << exe_path
                         << ", memory limit: " << mem_limit_mb << "MB";
            return RunResult(RunStatus::RUNTIME_ERROR, 128 + signal, signal);
        }

        LOG(ERROR) << "unknown program status, exe: " << exe_path;
        return RunResult(RunStatus::SYSTEM_ERROR);
    }
};

}  // namespace oj
