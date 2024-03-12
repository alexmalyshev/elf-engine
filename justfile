cwd := `pwd`

cc := `which clang`
cxx := `which clang++`

cxx_flags := '-stdlib=libc++ -std=c++23 -pedantic -Wall -Wextra'

default: build run

build_runtime:
    {{cxx}} {{cxx_flags}} -fPIC -shared runtime.cpp -o /tmp/runtime.so

build_writer:
    {{cxx}} {{cxx_flags}} runtime.cpp writer.cpp -o /tmp/writer

build_engine:
    {{cxx}} {{cxx_flags}} engine.cpp -o /tmp/engine

build: build_runtime build_writer build_engine

run_writer:
    /tmp/writer

run_readelf:
    readelf -a /tmp/test.so

run_engine:
    LD_LIBRARY_PATH={{cwd}} /tmp/engine /tmp/runtime.so

run: run_writer run_engine run_readelf
