#!/usr/bin/env sh
set -eu

BUILD_DIR=build-asan
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug -DEDGE_ENABLE_ASAN=ON
cmake --build "$BUILD_DIR" -j

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:print_summary=1:halt_on_error=1"
export UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"

ctest --test-dir "$BUILD_DIR" --output-on-failure
