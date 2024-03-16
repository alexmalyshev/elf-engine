cc := `which clang`
cxx := `which clang++`

cxx_flags := '-stdlib=libc++ -std=c++23 -pedantic -g -Wall -Wextra'

bundle := '/tmp/bundle.so'
runtime := '/tmp/runtime.so'
writer := '/tmp/writer'
engine := '/tmp/engine'

default: build run

build_runtime:
    {{cxx}} {{cxx_flags}} -fPIC -shared runtime.cpp -o {{runtime}}

build_writer:
    {{cxx}} {{cxx_flags}} runtime.cpp writer.cpp -o {{writer}}

build_engine:
    {{cxx}} {{cxx_flags}} engine.cpp -o {{engine}}

build: build_runtime build_writer build_engine

run_writer:
    {{writer}} {{bundle}}

run_engine:
    {{engine}} {{bundle}}

check_bundle:
    readelf -a {{bundle}}

clean:
    rm {{bundle}} {{runtime}} {{writer}} {{engine}} || true

run: run_writer run_engine
