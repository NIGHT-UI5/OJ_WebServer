#pragma once

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>

#include "log.hpp"

namespace oj {
namespace FileUtil {

inline bool Exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

inline bool CreateDir(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }

    if (errno == EEXIST) {
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            return true;
        }
    }

    LOG(ERROR) << "create directory failed: " << path;
    return false;
}

inline bool WriteFile(const std::string& path, const std::string& content) {
    std::ofstream file(path.c_str(), std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        LOG(ERROR) << "open file for write failed: " << path;
        return false;
    }

    file.write(content.data(), content.size());
    if (!file.good()) {
        LOG(ERROR) << "write file failed: " << path;
        return false;
    }

    return true;
}

inline bool ReadFile(const std::string& path, std::string* content) {
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        LOG(ERROR) << "open file for read failed: " << path;
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    *content = buffer.str();
    return true;
}

inline std::string JoinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) {
        return name;
    }

    if (dir[dir.size() - 1] == '/') {
        return dir + name;
    }

    return dir + "/" + name;
}

inline std::string GenerateTaskId() {
    static std::atomic<unsigned long> counter(0);

    std::ostringstream id;
    id << "task_"
       << static_cast<long long>(std::time(nullptr))
       << "_"
       << static_cast<long long>(getpid())
       << "_"
       << counter.fetch_add(1, std::memory_order_relaxed);
    return id.str();
}

inline bool CreateTaskDir(const std::string& base_dir,
                          std::string* task_id,
                          std::string* task_dir) {
    if (!CreateDir(base_dir)) {
        return false;
    }

    for (int i = 0; i < 10; ++i) {
        std::string id = GenerateTaskId();
        std::string dir = JoinPath(base_dir, id);
        // A task must get a newly created directory, never reuse an existing one.
        if (mkdir(dir.c_str(), 0755) == 0) {
            *task_id = id;
            *task_dir = dir;
            return true;
        }

        if (errno != EEXIST) {
            LOG(ERROR) << "create task directory failed: " << dir;
            return false;
        }
    }

    LOG(ERROR) << "create task directory failed, base dir: " << base_dir;
    return false;
}

}  // namespace FileUtil
}  // namespace oj
