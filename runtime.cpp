#include <print>

extern "C" {

extern "C" uint64_t collatz_step(uint64_t n) {
  return ((n & 1) == 0) ? (n / 2) : ((3 * n) + 1);
}

void* functionTable[1] = {reinterpret_cast<void*>(collatz_step)};

} // extern "C"
