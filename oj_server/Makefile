CXX := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -O2
COMMON_DIR := ../common

.PHONY: all clean

all: oj_server

oj_server: oj_server.cc load_balancer.hpp node_health_checker.hpp problem_model.hpp $(COMMON_DIR)/httplib.h $(COMMON_DIR)/log.hpp
	$(CXX) $(CXXFLAGS) oj_server.cc -o oj_server -pthread -ljsoncpp

clean:
	rm -f oj_server
