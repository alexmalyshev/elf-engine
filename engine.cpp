#include "runtime.h"

#include <dlfcn.h>

#include <cassert>
#include <print>

namespace {

size_t collatzConjecture(uint64_t (*collatzStep)(uint64_t), uint64_t n) {
  assert(n > 0);

  size_t steps = 0;
  while (n != 1) {
    n = collatzStep(n);
    ++steps;
  }

  return steps;
}

} // namespace

int main_runtime(int argc, char** argv) {
  if (argc != 2) {
    std::println(
      stderr,
      "Error: Takes exactly one argument (the shared object)"
    );
    return 1;
  }

  auto runtimePath = argv[1];
  auto handle = dlopen(runtimePath, RTLD_LOCAL | RTLD_NOW);
  if (handle == nullptr) {
    std::println(stderr, "Failed to open {}", runtimePath);
    return 1;
  }

  constexpr auto funcName = "collatz_step";
  auto symbol = dlsym(handle, funcName);
  if (symbol == nullptr) {
    std::println(stderr, "Failed to read function '{}'", funcName);
    return 1;
  }

  auto collatzStep = reinterpret_cast<uint64_t(*)(uint64_t)>(symbol);
  uint64_t n = 1457;
  std::println(
    "Collatz Conjecture for {} resolves in {} steps",
    n,
    collatzConjecture(collatzStep, n)
  );

  return 0;
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::println(
      stderr,
      "Error: Takes exactly one argument (the shared object)"
    );
    return 1;
  }

  auto bundlePath = argv[1];
  dlerror();
  auto handle = dlopen(bundlePath, RTLD_LOCAL | RTLD_NOW);
  if (handle == nullptr) {
    std::println(
      stderr,
      "Error: Failed to open {}\n\n{}\n",
      bundlePath,
      dlerror()
    );
    return 1;
  }

  constexpr auto funcName = "collatz_conjecture";
  dlerror();
  auto symbol = dlsym(handle, funcName);
  if (symbol == nullptr) {
    std::println(
      stderr,
      "Error: Failed to read function '{}'\n\n{}\n",
      funcName,
      dlerror()
    );
    return 1;
  }

  auto collatzConjecture = reinterpret_cast<size_t(*)(uint64_t)>(symbol);
  uint64_t n = 1457;
  std::println(
    "Collatz Conjecture for {} resolves in {} steps",
    n,
    collatzConjecture(n)
  );

  return 0;
}
