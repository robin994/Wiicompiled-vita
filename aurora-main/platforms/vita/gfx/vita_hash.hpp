#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace aurora::vita::gfx {
inline uint64_t fnv1a64(const void* data, size_t size, uint64_t seed = 14695981039346656037ull) noexcept {
  const auto* p = static_cast<const uint8_t*>(data);
  uint64_t h = seed;
  for (size_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}
template <typename T>
inline uint64_t hash_pod(const T& value, uint64_t seed = 14695981039346656037ull) noexcept {
  static_assert(std::is_trivially_copyable_v<T>);
  return fnv1a64(&value, sizeof(value), seed);
}
inline uint64_t hash_combine(uint64_t h, uint64_t v) noexcept {
  h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
  return h;
}
} // namespace aurora::vita::gfx
