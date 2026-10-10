#pragma once

#include "wiicompiled/disc.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wiicompiled {

struct DiscIndexEntry {
    std::string path;
    uint64_t offset = 0;
    uint32_t size = 0;
    bool directory = false;
};

struct DiscIndex {
    std::string gameId;
    uint64_t isoSize = 0;
    uint64_t partitionSize = 0;
    std::vector<DiscIndexEntry> entries;
};

// Portable little-endian metadata stored beside extracted disc files in a PSARC.
// It preserves the original decrypted-partition extents needed by DVDLowRead.
std::vector<uint8_t> encodeDiscIndex(const Disc& disc);
DiscIndex decodeDiscIndex(const void* data, size_t size);

using DiscIndexRead = bool (*)(void*, const std::string&, uint64_t, void*, size_t);
// Reads only ranges covered by original files. Missing padding/raw encrypted
// sectors are rejected; they are never replaced with fabricated zero bytes.
void readDiscIndex(const DiscIndex& index, uint64_t offset, void* destination,
                   size_t length, DiscIndexRead reader, void* context);

} // namespace wiicompiled
