#pragma once

#include "../common/httplib.h"
#include "../common/log.hpp"
#include "load_balancer.hpp"

#include <jsoncpp/json/json.h>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <sstream>
#include <thread>

namespace oj {

class NodeHealthChecker {
public:
    explicit NodeHealthChecker(LoadBalancer& balancer)
        : balancer_(balancer), stopped_(false), worker_(&NodeHealthChecker::Run, this) {}

    ~NodeHealthChecker() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        wake_.notify_all();
        worker_.join();
    }

    NodeHealthChecker(const NodeHealthChecker&) = delete;
    NodeHealthChecker& operator=(const NodeHealthChecker&) = delete;

private:
    static bool IsHealthy(const CompileNode& node) {
        httplib::Client client(node.host, node.port);
        client.set_connection_timeout(1, 0);
        client.set_read_timeout(1, 0);
        client.set_write_timeout(1, 0);
        client.set_max_timeout(std::chrono::seconds(1));
        const auto result = client.Get("/health");
        if (!result || result->status != 200) {
            return false;
        }
        Json::CharReaderBuilder reader;
        reader["failIfExtra"] = true;
        Json::Value body;
        std::string error;
        std::istringstream input(result->body);
        return Json::parseFromStream(reader, input, &body, &error) && body.isObject() &&
               body["ok"].isBool() && body["ok"].asBool() && body["service"].isString() &&
               body["service"].asString() == "compile_server";
    }

    void Run() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!wake_.wait_for(lock, std::chrono::seconds(2), [this]() { return stopped_; })) {
            lock.unlock();
            const auto nodes = balancer_.UnavailableNodes();
            for (const auto& entry : nodes) {
                {
                    std::lock_guard<std::mutex> stop_lock(mutex_);
                    if (stopped_) {
                        return;
                    }
                }
                // Neither the checker mutex nor the balancer mutex is held during I/O.
                try {
                    if (IsHealthy(entry.node) && balancer_.TryRecover(entry)) {
                        LOG(INFO) << "compile server recovered: " << entry.node.host << ":" << entry.node.port;
                    }
                } catch (const std::exception& error) {
                    LOG(WARNING) << "compile server health check failed: " << entry.node.host << ":"
                                 << entry.node.port << ": " << error.what();
                }
            }
            lock.lock();
        }
    }

    LoadBalancer& balancer_;
    bool stopped_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread worker_;
};

}  // namespace oj
