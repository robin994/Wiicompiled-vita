#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wiicompiled::vita::psarc {

bool Mount(const char* archivePath) noexcept;
void Shutdown() noexcept;
bool IsMounted() noexcept;
const char* LastError() noexcept;
struct Stats { uint64_t reads=0,bytes=0,ioMicros=0,failures=0; };
Stats Statistics() noexcept;

// Paths are relative archive paths, e.g. "disc/Race/foo.szs".
bool Read(const std::string& path, uint64_t offset, void* destination, size_t bytes) noexcept;
bool Size(const std::string& path, uint64_t& bytes) noexcept;
bool ReadWhole(const std::string& path, std::vector<uint8_t>& output,
               size_t maxBytes = 16 * 1024 * 1024) noexcept;

} // namespace wiicompiled::vita::psarc
