#pragma once
#include "thp_native.hpp"

namespace wic::avc {
constexpr size_t HeaderBytes = 24;
constexpr size_t MaxComponentBytes = 1024*1024;
struct Component { unsigned width=0, height=0; std::span<const uint8_t> elementary; };
bool Parse(std::span<const uint8_t>, Component&, std::string& error);
// Decoder-owned, pitched NV12 output is copied to the same planar contract
// used by the game texture packer. No guest/GXM operations occur here.
bool CopyNv12(std::span<const uint8_t> y, std::span<const uint8_t> uv,
              unsigned pitch, unsigned width, unsigned height, wic::thp::Image&, std::string& error);
// Accepted data can produce no picture yet. Pending leaves Image untouched;
// callers must not publish its old contents or report an SDK decode failure.
enum class DecodeResult { Error, Pending, Picture };
DecodeResult DecodeHardware(std::span<const uint8_t>, wic::thp::Image&, std::string& error);
}
