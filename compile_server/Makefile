CXX := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -O2
COMMON_DIR := ../common

.PHONY: all clean

all: compile_server

compile_server: compile_server.cc compiler.hpp runner.hpp judge.hpp $(COMMON_DIR)/httplib.h $(COMMON_DIR)/file_util.hpp $(COMMON_DIR)/log.hpp
	$(CXX) $(CXXFLAGS) compile_server.cc -o compile_server -pthread -ljsoncpp

clean:
	rm -f compile_server
