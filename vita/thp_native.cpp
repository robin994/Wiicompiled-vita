#include "thp_native.hpp"
#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <memory>
#include <jpeglib.h>

namespace wic::thp {
namespace {
unsigned Be16(const uint8_t* p) { return unsigned(p[0]) * 256 + p[1]; }

bool Prepare(std::span<const uint8_t> input, std::vector<uint8_t>& jpeg,
             unsigned& width, unsigned& height, std::string& error) {
    if (input.size() < 4 || input.size() > 1024*1024 || input[0] != 0xff || input[1] != 0xd8) {
        error = "invalid THP component length or SOI"; return false;
    }
    size_t scan = 0;
    bool frame = false;
    for (size_t pos = 2; pos + 4 <= input.size();) {
        if (input[pos] != 0xff) break;
        const auto marker = input[pos+1];
        const auto length = Be16(input.data()+pos+2);
        if (length < 2 || length > input.size()-pos-2) break;
        const auto end = pos+2+length;
        if (marker == 0xc0) {
            if (length != 17 || input[pos+4] != 8 || input[pos+9] != 3 ||
                input[pos+11] != 0x22 || input[pos+14] != 0x11 || input[pos+17] != 0x11) break;
            height = Be16(input.data()+pos+5); width = Be16(input.data()+pos+7);
            if (!width || !height || width > 1024 || height > 528 || width%16 || height%8) break;
            frame = true;
        } else if (marker == 0xda) {
            if (!frame || length != 12 || input[pos+4] != 3 ||
                input[end-3] != 0 || input[end-2] != 63 || input[end-1] != 0) break;
            scan = end; break;
        } else if (marker == 0xdd) {
            // Unescaped entropy cannot distinguish restart markers from data.
            if (length != 4 || Be16(input.data()+pos+4) != 0) break;
        } else if (marker != 0xdb && marker != 0xc4 && marker != 0xfe &&
                   !(marker >= 0xe0 && marker <= 0xef)) break;
        pos = end;
    }
    if (!scan) { error = "unsupported THP JPEG header"; return false; }
    // Locate only the terminal EOI, never the first FF/D9 in the unescaped
    // entropy. THP's component table rounds its byte length to four bytes.
    size_t end = input.size();
    unsigned padding = 0;
    while (padding < 3 && end > scan+2 && input[end-1] == 0) { --end; ++padding; }
    if (end < scan+2 || input[end-2] != 0xff || input[end-1] != 0xd9) {
        error = "missing terminal EOI"; return false;
    }
    end -= 2;
    jpeg.clear(); jpeg.reserve(scan+2*(end-scan)+2);
    jpeg.insert(jpeg.end(), input.begin(), input.begin()+scan);
    for (size_t pos = scan; pos < end; ++pos) {
        jpeg.push_back(input[pos]);
        if (input[pos] == 0xff) jpeg.push_back(0);
    }
    jpeg.push_back(0xff); jpeg.push_back(0xd9);
    return true;
}

struct JpegState {
    jpeg_decompress_struct decoder{};
    struct Error { jpeg_error_mgr manager{}; std::jmp_buf jump; char message[JMSG_LENGTH_MAX]{}; } error;
    bool created = false;
    ~JpegState() { if (created) jpeg_destroy_decompress(&decoder); }
};
void ErrorExit(j_common_ptr decoder) {
    auto* error = reinterpret_cast<JpegState::Error*>(decoder->err);
    decoder->err->format_message(decoder, error->message);
    std::longjmp(error->jump, 1);
}
void Message(j_common_ptr decoder, int level) { if (level < 0) ErrorExit(decoder); }
}

bool Decode(std::span<const uint8_t> component, Image& image, std::string& error) {
    // All non-trivial automatic objects exist before setjmp. libjpeg longjmp
    // crosses only trivial row arrays/counters; its mutable state is on heap.
    std::vector<uint8_t> jpeg;
    unsigned width = 0, height = 0;
    if (!Prepare(component, jpeg, width, height, error)) return false;
    auto state = std::make_unique<JpegState>();
    std::array<std::vector<uint8_t>, 3> rows;
    rows[0].resize(size_t(width)*16); rows[1].resize(size_t(width/2)*8); rows[2].resize(size_t(width/2)*8);
    image.planes[0].resize(size_t(width)*height);
    image.planes[1].resize(size_t(width/2)*(height/2)); image.planes[2].resize(size_t(width/2)*(height/2));
    auto& d = state->decoder;
    d.err = jpeg_std_error(&state->error.manager);
    d.err->error_exit = ErrorExit; d.err->emit_message = Message;
    if (setjmp(state->error.jump)) { error = state->error.message; return false; }
    state->created = true; jpeg_create_decompress(&d);
    jpeg_mem_src(&d, jpeg.data(), jpeg.size());
    if (jpeg_read_header(&d, TRUE) != JPEG_HEADER_OK || d.image_width != width || d.image_height != height ||
        d.num_components != 3 || d.jpeg_color_space != JCS_YCbCr || d.progressive_mode) {
        error = "unsupported decoded THP geometry"; return false;
    }
    d.raw_data_out = TRUE; d.do_fancy_upsampling = FALSE; d.dct_method = JDCT_ISLOW;
    if (!jpeg_start_decompress(&d)) { error = "JPEG startup suspended"; return false; }
    for (unsigned y = 0; y < height; y += 16) {
        JSAMPROW yRows[16], uRows[8], vRows[8];
        for (unsigned i=0; i<16; ++i) yRows[i] = rows[0].data()+size_t(i)*width;
        for (unsigned i=0; i<8; ++i) { uRows[i] = rows[1].data()+size_t(i)*(width/2); vRows[i] = rows[2].data()+size_t(i)*(width/2); }
        JSAMPARRAY buffers[] = {yRows, uRows, vRows};
        if (jpeg_read_raw_data(&d, buffers, 16) != 16) { error = "JPEG raw output suspended"; return false; }
        const auto count = std::min(16u, height-y);
        std::memcpy(image.planes[0].data()+size_t(y)*width, rows[0].data(), size_t(count)*width);
        std::memcpy(image.planes[1].data()+size_t(y/2)*(width/2), rows[1].data(), size_t(count/2)*(width/2));
        std::memcpy(image.planes[2].data()+size_t(y/2)*(width/2), rows[2].data(), size_t(count/2)*(width/2));
    }
    if (!jpeg_finish_decompress(&d)) { error = "JPEG finish suspended"; return false; }
    image.width = width; image.height = height; error.clear(); return true;
}

void TileRange(const uint8_t* source, uint8_t* destination, unsigned width, size_t begin, size_t end) noexcept {
    const auto columns = width/8;
    for (size_t tile = begin; tile < end; ++tile) {
        const size_t y = (tile/columns)*4, x = (tile%columns)*8;
        for (unsigned row = 0; row<4; ++row)
            std::memcpy(destination+tile*32+row*8, source+(y+row)*width+x, 8);
    }
}
}
