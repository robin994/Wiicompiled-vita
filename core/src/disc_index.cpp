#include "wiicompiled/disc_index.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace wiicompiled {
namespace {

constexpr char kMagic[8] = {'W','I','C','I','D','X','0','1'};
constexpr uint32_t kVersion = 1;
constexpr size_t kHeaderSize = 8 + 4 + 4 + 8 + 8 + 8;

void append16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}
void append32(std::vector<uint8_t>& out, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
void append64(std::vector<uint8_t>& out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
uint16_t read16(const uint8_t*& p, const uint8_t* end) {
    if (static_cast<size_t>(end - p) < 2) throw std::runtime_error("Truncated disc index");
    const uint16_t value = static_cast<uint16_t>(p[0]) |
                           static_cast<uint16_t>(p[1]) << 8;
    p += 2;
    return value;
}
uint32_t read32(const uint8_t*& p, const uint8_t* end) {
    if (static_cast<size_t>(end - p) < 4) throw std::runtime_error("Truncated disc index");
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<uint32_t>(p[i]) << (i * 8);
    p += 4;
    return value;
}
uint64_t read64(const uint8_t*& p, const uint8_t* end) {
    if (static_cast<size_t>(end - p) < 8) throw std::runtime_error("Truncated disc index");
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= static_cast<uint64_t>(p[i]) << (i * 8);
    p += 8;
    return value;
}
std::string canonical(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.empty() || path.front() != '/') path.insert(path.begin(), '/');
    for (char& c : path) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return path;
}

} // namespace

std::vector<uint8_t> encodeDiscIndex(const Disc& disc) {
    const auto& entries = disc.entries();
    if (entries.size() > std::numeric_limits<uint32_t>::max())
        throw std::runtime_error("Too many disc index entries");

    std::vector<uint8_t> out;
    out.reserve(kHeaderSize + entries.size() * 32);
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    append32(out, kVersion);
    append32(out, static_cast<uint32_t>(entries.size()));
    append64(out, disc.isoSize());
    append64(out, disc.partitionSize());
    char gameId[8]{};
    const auto& id = disc.gameId();
    if (id.size() > sizeof(gameId)) throw std::runtime_error("Disc game ID is too long");
    std::memcpy(gameId, id.data(), id.size());
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(gameId),
               reinterpret_cast<const uint8_t*>(gameId) + sizeof(gameId));

    for (const auto& entry : entries) {
        if (entry.path.empty() || entry.path.size() > std::numeric_limits<uint16_t>::max())
            throw std::runtime_error("Invalid disc index path");
        append64(out, entry.offset);
        append32(out, entry.size);
        append16(out, static_cast<uint16_t>(entry.path.size()));
        append16(out, entry.directory ? 1u : 0u);
        out.insert(out.end(), entry.path.begin(), entry.path.end());
    }
    return out;
}

DiscIndex decodeDiscIndex(const void* data, size_t size) {
    if (!data || size < kHeaderSize) throw std::runtime_error("Truncated disc index");
    const auto* begin = static_cast<const uint8_t*>(data);
    const auto* p = begin;
    const auto* end = begin + size;
    if (std::memcmp(p, kMagic, sizeof(kMagic)) != 0)
        throw std::runtime_error("Invalid disc index magic");
    p += sizeof(kMagic);
    if (read32(p, end) != kVersion) throw std::runtime_error("Unsupported disc index version");
    const uint32_t count = read32(p, end);

    DiscIndex result;
    result.isoSize = read64(p, end);
    result.partitionSize = read64(p, end);
    if (static_cast<size_t>(end - p) < 8) throw std::runtime_error("Truncated disc index game ID");
    const char* id = reinterpret_cast<const char*>(p);
    size_t idLength = 0;
    while (idLength < 8 && id[idLength]) ++idLength;
    result.gameId.assign(id, idLength);
    p += 8;
    if (result.gameId.empty()) throw std::runtime_error("Disc index has no game ID");

    if (count > 65536 || count > static_cast<size_t>(end-p)/17)
        throw std::runtime_error("Invalid disc index entry count");

    result.entries.reserve(count);
    std::unordered_set<std::string> paths;
    for (uint32_t i = 0; i < count; ++i) {
        DiscIndexEntry entry;
        entry.offset = read64(p, end);
        entry.size = read32(p, end);
        const uint16_t pathLength = read16(p, end);
        const uint16_t flags = read16(p, end);
        if (flags & ~1u) throw std::runtime_error("Unsupported disc index entry flags");
        if (!pathLength || static_cast<size_t>(end - p) < pathLength)
            throw std::runtime_error("Truncated disc index path");
        entry.path.assign(reinterpret_cast<const char*>(p), pathLength);
        p += pathLength;
        if (entry.path.front() != '/' || entry.path.back() == '/' ||
            entry.path.find_first_of("\\:\n\r") != std::string::npos)
            throw std::runtime_error("Unsafe disc index path");
        size_t component=1;
        for(size_t j=1;j<=entry.path.size();++j) {
            if(j<entry.path.size() && static_cast<unsigned char>(entry.path[j])<32)
                throw std::runtime_error("Control byte in disc index path");
            if(j==entry.path.size() || entry.path[j]=='/') {
                const auto name=entry.path.substr(component,j-component);
                if(name.empty() || name=="." || name=="..")throw std::runtime_error("Unsafe disc index component");
                component=j+1;
            }
        }
        entry.directory = (flags & 1u) != 0;
        if(entry.directory && (entry.size || entry.offset))throw std::runtime_error("Invalid disc index directory");
        if (!entry.directory && (entry.offset > result.partitionSize ||
            entry.size > result.partitionSize - entry.offset))
            throw std::runtime_error("Disc index entry outside partition");
        if (!paths.emplace(canonical(entry.path)).second)
            throw std::runtime_error("Duplicate disc index path");
        result.entries.push_back(std::move(entry));
    }
    if (p != end) throw std::runtime_error("Disc index has trailing bytes");
    return result;
}

void readDiscIndex(const DiscIndex& index,uint64_t offset,void* destination,
                   size_t length,DiscIndexRead reader,void* context) {
    if(offset>index.partitionSize || length>index.partitionSize-offset ||
        (!destination && length) || !reader)throw std::runtime_error("Packed partition read outside bounds");
    struct Part { const DiscIndexEntry* entry;uint64_t bias;size_t length; };
    std::vector<Part> parts;
    uint64_t at=offset;size_t remaining=length;
    while(remaining) {
        const DiscIndexEntry* best=nullptr;
        for(const auto& e:index.entries)if(!e.directory && at>=e.offset && at-e.offset<e.size &&
            (!best || e.offset+e.size>best->offset+best->size))best=&e;
        if(!best)throw std::runtime_error("Packed partition read touches discarded padding");
        const size_t take=static_cast<size_t>(std::min<uint64_t>(remaining,best->size-(at-best->offset)));
        parts.push_back({best,at-best->offset,take});at+=take;remaining-=take;
    }
    auto* out=static_cast<uint8_t*>(destination);
    for(const auto& part:parts) {
        if(!reader(context,"disc"+part.entry->path,part.bias,out,part.length))
            throw std::runtime_error("Packed partition short read");
        out+=part.length;
    }
}

} // namespace wiicompiled
