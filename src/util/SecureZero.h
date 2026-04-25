#pragma once
#include <cstddef>
#include <cstdint>

namespace util {
  void secureZero(void* p, std::size_t n) noexcept;
}
