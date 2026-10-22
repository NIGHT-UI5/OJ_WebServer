#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace oj {

struct CompileNode {
    std::string host;
    int port;
};

class LoadBalancer {
public:
    // The node list is fixed at startup; counts describe requests OJ is waiting for.
    explicit LoadBalancer(const std::vector<CompileNode>& nodes)
        : nodes_(nodes), states_(nodes.size()), next_(0) {}

    struct UnavailableNode {
        std::size_t index;
        std::size_t generation;
        CompileNode node;
    };

    // Keep this local to a request. The balancer must outlive every guard.
    class RequestGuard {
    public:
        explicit RequestGuard(LoadBalancer& balancer)
            : balancer_(balancer), index_(0), generation_(0), node_{"", 0},
              valid_(balancer_.Acquire(index_, generation_, node_)) {}

        ~RequestGuard() {
            if (valid_) {
                balancer_.Release(index_);
            }
        }

        RequestGuard(const RequestGuard&) = delete;
        RequestGuard& operator=(const RequestGuard&) = delete;

        bool Valid() const { return valid_; }
        const CompileNode& Node() const { return node_; }
        bool MarkUnavailable() {
            return valid_ && balancer_.MarkUnavailable(index_, generation_);
        }

    private:
        LoadBalancer& balancer_;
        std::size_t index_;
        std::size_t generation_;
        CompileNode node_;
        bool valid_;
    };

    std::vector<std::size_t> ActiveRequests() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::size_t> counts;
        for (const auto& state : states_) {
            counts.push_back(state.active_requests);
        }
        return counts;
    }

    std::vector<UnavailableNode> UnavailableNodes() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<UnavailableNode> nodes;
        for (std::size_t i = 0; i < states_.size(); ++i) {
            if (!states_[i].available) {
                nodes.push_back({i, states_[i].generation, nodes_[i]});
            }
        }
        return nodes;
    }

    bool TryRecover(const UnavailableNode& node) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (node.index >= states_.size()) {
            return false;
        }
        NodeState& state = states_[node.index];
        if (state.available || state.generation != node.generation) {
            return false;
        }
        state.available = true;
        ++state.generation;
        return true;
    }

private:
    struct NodeState {
        std::size_t active_requests = 0;
        bool available = true;
        std::size_t generation = 0;
    };

    bool Acquire(std::size_t& index, std::size_t& generation, CompileNode& node) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (nodes_.empty()) {
            return false;
        }
        // Scan from the cursor so equally busy nodes take turns.
        std::size_t best = nodes_.size();
        for (std::size_t offset = 0; offset < nodes_.size(); ++offset) {
            const std::size_t candidate = (next_ + offset) % nodes_.size();
            if (states_[candidate].available && (best == nodes_.size() ||
                states_[candidate].active_requests < states_[best].active_requests)) {
                best = candidate;
            }
        }
        if (best == nodes_.size()) {
            return false;
        }
        node = nodes_[best];
        index = best;
        generation = states_[best].generation;
        ++states_[best].active_requests;
        next_ = (best + 1) % nodes_.size();
        return true;
    }

    void Release(std::size_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        --states_[index].active_requests;
    }

    bool MarkUnavailable(std::size_t index, std::size_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        NodeState& state = states_[index];
        // Results from requests started before a recovery must not remove the recovered node.
        if (!state.available || state.generation != generation) {
            return false;
        }
        state.available = false;
        ++state.generation;
        return true;
    }

    const std::vector<CompileNode> nodes_;
    std::vector<NodeState> states_;
    std::size_t next_;
    mutable std::mutex mutex_;
};

}  // namespace oj
