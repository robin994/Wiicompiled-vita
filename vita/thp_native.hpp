#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace wic::thp {
struct Image {
    unsigned width = 0, height = 0;
    std::array<std::vector<uint8_t>, 3> planes;
};
// THP stores baseline JPEG entropy without FF/00 stuffing. The caller supplies
// the component length from the THP record, including at most three pad bytes.
bool Decode(std::span<const uint8_t> component, Image& image, std::string& error);
// Each item is one GX I8 8x4 tile. Ranges are disjoint and source is immutable.
void TileRange(const uint8_t* source, uint8_t* destination, unsigned width,
               size_t begin, size_t end) noexcept;
}
