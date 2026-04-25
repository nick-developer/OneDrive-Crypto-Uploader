#include "SecureZero.h"

namespace util {
void secureZero(void* p, std::size_t n) noexcept {
  volatile std::uint8_t* v = reinterpret_cast<volatile std::uint8_t*>(p);
  while (n--) *v++ = 0;
}
}
