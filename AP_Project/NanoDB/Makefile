# Hand-written GNU Makefile for NanoDB.
# Mirrors CMakeLists.txt for users without CMake.
#
# Usage:
#   make            -> build test_runner and gen_tpch
#   make run        -> build and run the test harness
#   make clean      -> remove build artifacts

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -iquote include
LDFLAGS  ?=

SRC := \
    src/String.cpp \
    src/DBValue.cpp \
    src/Tokenizer.cpp \
    src/Parser.cpp \
    src/Evaluator.cpp \
    src/Graph.cpp \
    src/DiskManager.cpp \
    src/BufferPool.cpp \
    src/Logger.cpp \
    src/Table.cpp \
    src/Engine.cpp

OBJDIR := build
OBJ := $(SRC:%.cpp=$(OBJDIR)/%.o)

TEST_BIN  := test_runner
GEN_BIN   := gen_tpch

all: $(TEST_BIN) $(GEN_BIN)

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_BIN): $(OBJ) $(OBJDIR)/test_runner.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJDIR)/test_runner.o: test_runner.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(GEN_BIN): tools/gen_tpch.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

run: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf $(OBJDIR) $(TEST_BIN) $(TEST_BIN).exe $(GEN_BIN) $(GEN_BIN).exe nanodb_execution.log

distclean: clean
	rm -rf data

.PHONY: all run clean distclean
