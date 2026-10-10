#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wiicompiled {

struct DiscEntry {
    std::string path;
    uint64_t offset = 0; // Decrypted partition byte offset, never a raw ISO offset.
    uint32_t size = 0;
    bool directory = false;
};

struct DiscStats {
    uint64_t rawBytes = 0, requestedBytes = 0, cacheHits = 0, cacheMisses = 0;
    uint64_t rawReadMicros = 0, decryptHashMicros = 0;
};

// Read-only clean Wii ISO. One owner and one bounded decrypted-cluster cache;
// reads are serialized so audio/DVD callers cannot race the cache or file seek.
class Disc {
public:
    explicit Disc(size_t cacheClusters = 16);
    ~Disc();
    Disc(const Disc&) = delete;
    Disc& operator=(const Disc&) = delete;

    void open(const std::string& isoPath);
    void read(uint64_t partitionOffset, void* destination, size_t length);
    void readRaw(uint64_t isoOffset, void* destination, size_t length);
    void readFile(const std::string& path, uint64_t offset, void* destination, size_t length);
    std::vector<uint8_t> file(const std::string& path, size_t maxSize = 32 * 1024 * 1024);
    const DiscEntry* find(const std::string& path) const;
    const std::vector<DiscEntry>& entries() const;
    const std::vector<uint8_t>& fst() const;
    const std::string& gameId() const;
    uint64_t isoSize() const;
    uint64_t partitionSize() const;
    DiscStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace wiicompiled
