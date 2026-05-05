@echo off
setlocal

set CXX=C:\mingw64\bin\g++.exe
set FLAGS=-std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -iquote include

set SRCS=src/String.cpp src/DBValue.cpp src/Tokenizer.cpp src/Parser.cpp src/Evaluator.cpp src/Graph.cpp src/DiskManager.cpp src/BufferPool.cpp src/Logger.cpp src/Table.cpp src/Engine.cpp

echo [1/2] Compiling NanoDB...
%CXX% %FLAGS% %SRCS% test_runner.cpp -o build\test_runner.exe
if errorlevel 1 (
    echo COMPILE FAILED
    exit /b 1
)
echo [2/2] Build successful: build\test_runner.exe

if not exist data mkdir data
echo Running NanoDB...
build\test_runner.exe
