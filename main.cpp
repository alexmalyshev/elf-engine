#include <print>

#include <dlfcn.h>

int main(int, char**) {
  constexpr auto kRuntime = "runtime.so";

  auto handle = dlopen("runtime.so", RTLD_LOCAL | RTLD_NOW);
  if (handle == nullptr) {
    std::println(stderr, "Failed to open {}", kRuntime);
    return 1;
  }

  constexpr auto greeterName = "sayHello";
  auto symbol = dlsym(handle, greeterName);
  if (symbol == nullptr) {
    std::println(stderr, "Failed to read function '{}'", greeterName);
    return 1;
  }

  auto greeter = reinterpret_cast<void(*)()>(symbol);
  greeter();

  return 0;
}
