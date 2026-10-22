#pragma once

#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

#include "../common/log.hpp"

namespace oj {

class Compiler {
public:
    static bool Compile(const std::string& src_path,
                        const std::string& exe_path,
                        const std::string& err_path) {
        pid_t pid = fork();
        if (pid < 0) {
            LOG(ERROR) << "fork failed while compiling: " << src_path;
            return false;
        }

        if (pid == 0) {
            int err_fd = open(err_path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
            if (err_fd < 0) {
                LOG(ERROR) << "open compile error file failed: " << err_path;
                _exit(2);
            }

            dup2(err_fd, STDERR_FILENO);
            close(err_fd);

            execlp("g++",
                   "g++",
                   src_path.c_str(),
                   "-o",
                   exe_path.c_str(),
                   "-std=c++11",
                   nullptr);

            LOG(ERROR) << "exec g++ failed, src: " << src_path;
            _exit(3);
        }

        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            LOG(ERROR) << "waitpid failed while compiling: " << src_path;
            return false;
        }

        bool success = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (success) {
            LOG(INFO) << "compile success, src: " << src_path << ", exe: " << exe_path;
        } else {
            LOG(WARNING) << "compile failed, src: " << src_path << ", err: " << err_path;
        }

        return success;
    }
};

}  // namespace oj
