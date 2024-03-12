cwd := `pwd`

cc := `which clang`
cxx := `which clang++`

cxx_flags := '-stdlib=libc++ -std=c++23 -pedantic -Wall -Wextra'

default: build run

build_runtime:
    {{cxx}} {{cxx_flags}} -fPIC -shared runtime.cpp -o /tmp/runtime.so

build_writer:
    {{cxx}} {{cxx_flags}} runtime.cpp writer.cpp -o /tmp/writer

build_main:
    {{cxx}} {{cxx_flags}} main.cpp -o /tmp/main

build: build_runtime build_writer build_main

run_writer:
    /tmp/writer

run_readelf:
    readelf -a /tmp/test.so

run_main:
    LD_LIBRARY_PATH={{cwd}} /tmp/main

run: run_writer run_readelf
