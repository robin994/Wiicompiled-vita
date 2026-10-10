#include "wiicompiled/disc.hpp"

#include <openssl/aes.h>
#include <openssl/sha.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <chrono>
#if defined(__vita__)
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#else
#include <sys/types.h>
#endif

namespace wiicompiled {
namespace {
constexpr size_t kCluster = 0x8000, kHeader = 0x400, kPayload = 0x7c00;
constexpr uint32_t kWiiMagic = 0x5d1c9ea3;
constexpr std::array<uint8_t, 16> kRetailKey{
    0xeb,0xe4,0x2a,0x22,0x5e,0x85,0x93,0xe4,0x48,0xd9,0xc5,0x45,0x73,0x81,0xaa,0xf7};
uint64_t micros() {
#if defined(__vita__)
    // Guest Wii threads execute on libco stacks which are deliberately outside
    // the native Vita thread stack. A Vita syscall from such a stack can be
    // rejected by the kernel as SCE_COREDUMP_STOP_REASON_STACK_OVERFLOW.
    // Vita I/O timing is collected by the native ISO worker below instead.
    return 0;
#else
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

void require(bool ok, const char* reason);

#if defined(__vita__)
class DiscMutex {
public:
    void lock() noexcept {
        while (flag_.test_and_set(std::memory_order_acquire)) {
            __asm__ volatile("yield" ::: "memory");
        }
    }
    void unlock() noexcept { flag_.clear(std::memory_order_release); }
private:
    std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
};

enum class VitaIoOp : uint32_t {
    None = 0,
    Open,
    Read,
    Close,
};

struct VitaIoWorker {
    // 0 = idle, 1 = request published, 2 = completed.
    std::atomic<uint32_t> state{0};
    SceUID thread = -1;
    int fd = -1;
    VitaIoOp op = VitaIoOp::None;
    const char* path = nullptr;
    uint64_t offset = 0;
    void* destination = nullptr;
    size_t length = 0;
    int result = -1;
    uint64_t value = 0;
    uint64_t elapsedMicros = 0;
};

VitaIoWorker g_vitaIo;

inline void VitaFiberRelax() noexcept {
    // This is intentionally not sceKernelDelayThread/sceKernelDelayThreadCB:
    // callers can be executing on a libco stack, where entering the Vita kernel
    // would make the alternate SP look like a native thread stack overflow.
    __asm__ volatile("yield" ::: "memory");
}

int VitaIoThread(SceSize, void*) {
    for (;;) {
        if (g_vitaIo.state.load(std::memory_order_acquire) != 1u) {
            sceKernelDelayThread(100);
            continue;
        }

        int result = -1;
        uint64_t value = 0;
        uint64_t elapsed = 0;
        switch (g_vitaIo.op) {
        case VitaIoOp::Open: {
            if (g_vitaIo.fd >= 0) {
                sceIoClose(g_vitaIo.fd);
                g_vitaIo.fd = -1;
            }
            const int fd = sceIoOpen(g_vitaIo.path, SCE_O_RDONLY, 0);
            if (fd >= 0) {
                const SceOff end = sceIoLseek(fd, 0, SCE_SEEK_END);
                if (end >= 0) {
                    g_vitaIo.fd = fd;
                    value = static_cast<uint64_t>(end);
                    result = 0;
                } else {
                    sceIoClose(fd);
                    result = static_cast<int>(end);
                }
            } else {
                result = fd;
            }
            break;
        }
        case VitaIoOp::Read: {
            const uint64_t started = sceKernelGetProcessTimeWide();
            if (g_vitaIo.fd >= 0) {
                const SceOff requested = static_cast<SceOff>(g_vitaIo.offset);
                const SceOff positioned = sceIoLseek(g_vitaIo.fd, requested, SCE_SEEK_SET);
                if (positioned == requested) {
                    auto* out = static_cast<uint8_t*>(g_vitaIo.destination);
                    size_t remaining = g_vitaIo.length;
                    result = 0;
                    while (remaining != 0) {
                        const unsigned chunk = static_cast<unsigned>(
                            std::min<size_t>(remaining, 1024 * 1024));
                        const int got = sceIoRead(g_vitaIo.fd, out, chunk);
                        if (got <= 0) {
                            result = got < 0 ? got : -1;
                            break;
                        }
                        remaining -= static_cast<size_t>(got);
                        out += got;
                    }
                } else {
                    result = positioned < 0 ? static_cast<int>(positioned) : -1;
                }
            }
            elapsed = sceKernelGetProcessTimeWide() - started;
            break;
        }
        case VitaIoOp::Close:
            if (g_vitaIo.fd >= 0) {
                result = sceIoClose(g_vitaIo.fd);
                g_vitaIo.fd = -1;
            } else {
                result = 0;
            }
            break;
        case VitaIoOp::None:
            result = 0;
            break;
        }

        g_vitaIo.result = result;
        g_vitaIo.value = value;
        g_vitaIo.elapsedMicros = elapsed;
        g_vitaIo.state.store(2u, std::memory_order_release);
    }
}

void EnsureVitaIoWorker() {
    if (g_vitaIo.thread >= 0) return;
    const SceUID thread = sceKernelCreateThread(
        "wic_iso_io", VitaIoThread, 0x10000100, 64 * 1024, 0, 0, nullptr);
    require(thread >= 0, "Cannot create Vita ISO I/O worker");
    const int started = sceKernelStartThread(thread, 0, nullptr);
    if (started < 0) {
        sceKernelDeleteThread(thread);
        require(false, "Cannot start Vita ISO I/O worker");
    }
    g_vitaIo.thread = thread;
}

void SubmitVitaIo(VitaIoOp op) {
    // Requests are serialized by DiscMutex. Never block through a Vita kernel
    // primitive here: this function is intentionally safe on a libco stack.
    while (g_vitaIo.state.load(std::memory_order_acquire) != 0u) {
        VitaFiberRelax();
    }
    g_vitaIo.op = op;
    g_vitaIo.result = -1;
    g_vitaIo.value = 0;
    g_vitaIo.elapsedMicros = 0;
    g_vitaIo.state.store(1u, std::memory_order_release);
    while (g_vitaIo.state.load(std::memory_order_acquire) != 2u) {
        VitaFiberRelax();
    }
    g_vitaIo.state.store(0u, std::memory_order_release);
}
#else
using DiscMutex = std::mutex;
#endif

uint32_t be32(const uint8_t* p) {
    return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3];
}
void require(bool ok, const char* reason) {
    if (!ok) throw std::runtime_error(reason);
}
bool fits(uint64_t offset, uint64_t length, uint64_t size) {
    return offset <= size && length <= size - offset;
}
std::string canonical(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.empty() || path.front() != '/') path.insert(path.begin(), '/');
    for (char& c : path) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return path;
}

class RawFile {
public:
    ~RawFile() { close(); }
    void close() {
#if defined(__vita__)
        if (opened_) {
            SubmitVitaIo(VitaIoOp::Close);
            opened_ = false;
        }
#else
        if (file_) std::fclose(file_);
        file_ = nullptr;
#endif
        size_ = 0;
    }
    void open(const std::string& path) {
        close();
#if defined(__vita__)
        EnsureVitaIoWorker();
        g_vitaIo.path = path.c_str();
        SubmitVitaIo(VitaIoOp::Open);
        require(g_vitaIo.result >= 0, "Cannot open mariokart.iso");
        size_ = g_vitaIo.value;
        opened_ = true;
#else
        static_assert(sizeof(off_t) >= 8, "ISO reader needs 64-bit file offsets");
        file_ = std::fopen(path.c_str(), "rb");
        require(file_ != nullptr, "Cannot open mariokart.iso");
        require(fseeko(file_, 0, SEEK_END) == 0, "Cannot seek ISO");
        const off_t end = ftello(file_);
        require(end >= 0, "Cannot determine ISO size");
        size_ = static_cast<uint64_t>(end);
#endif
    }
    uint64_t size() const { return size_; }
    void read(uint64_t offset, void* destination, size_t length) {
        require(fits(offset, length, size_), "Read outside ISO (truncated image)");
#if defined(__vita__)
        lastReadMicros_ = 0;
#endif
        if (!length) return;
#if defined(__vita__)
        require(opened_, "ISO is not mounted");
        g_vitaIo.offset = offset;
        g_vitaIo.destination = destination;
        g_vitaIo.length = length;
        SubmitVitaIo(VitaIoOp::Read);
        lastReadMicros_ = g_vitaIo.elapsedMicros;
        require(g_vitaIo.result >= 0, "Short ISO read");
#else
        require(fseeko(file_, static_cast<off_t>(offset), SEEK_SET) == 0, "ISO seek failed");
        require(std::fread(destination, 1, length, file_) == length, "Short ISO read");
#endif
    }
    uint64_t lastReadMicros() const {
#if defined(__vita__)
        return lastReadMicros_;
#else
        return 0;
#endif
    }
private:
    uint64_t size_ = 0;
#if defined(__vita__)
    bool opened_ = false;
    uint64_t lastReadMicros_ = 0;
#else
    FILE* file_ = nullptr;
#endif
};
} // namespace

struct Disc::Impl {
    struct Cached {
        uint64_t index = UINT64_MAX, age = 0;
        std::array<uint8_t, kPayload> bytes{};
    };
    explicit Impl(size_t count) : cache(count) {
        require(count >= 1 && count <= 256, "ISO cache must have 1..256 clusters");
    }
    mutable DiscMutex mutex;
    RawFile raw;
    std::vector<Cached> cache;
    std::array<uint8_t, kCluster> encrypted{};
    std::array<uint8_t, kHeader> hashes{};
    AES_KEY titleKey{};
    uint64_t dataStart = 0, logicalSize = 0, clock = 0;
    std::string id;
    std::vector<DiscEntry> entries;
    std::vector<uint8_t> fst;
    std::unordered_map<std::string, size_t> paths;
    DiscStats counters{};
    bool ready = false;

    void rawRead(uint64_t offset, void* out, size_t length) {
        require(out || !length,"Null raw ISO read destination");
#if defined(__vita__)
        raw.read(offset, out, length);
        counters.rawReadMicros += raw.lastReadMicros();
#else
        const auto start=micros();raw.read(offset, out, length);
        counters.rawReadMicros+=micros()-start;
#endif
        counters.rawBytes+=length;
    }
    const Cached& cluster(uint64_t index) {
        for (auto& item : cache) if (item.index == index) {
            item.age = ++clock; ++counters.cacheHits; return item;
        }
        ++counters.cacheMisses;
        auto& item = *std::min_element(cache.begin(), cache.end(),
            [](const Cached& a, const Cached& b) { return a.age < b.age; });
        // A failed read/decrypt must never leave an old index pointing at new bytes.
        item.index = UINT64_MAX;
        rawRead(dataStart + index*kCluster, encrypted.data(), encrypted.size());
        const auto decryptStart=micros();
        std::array<uint8_t,16> iv{};
        AES_cbc_encrypt(encrypted.data(), hashes.data(), hashes.size(), &titleKey, iv.data(), AES_DECRYPT);
        std::memcpy(iv.data(), encrypted.data()+0x3d0, iv.size());
        AES_cbc_encrypt(encrypted.data()+kHeader, item.bytes.data(), item.bytes.size(),
                        &titleKey, iv.data(), AES_DECRYPT);
        for (size_t block=0; block<31; ++block) {
            std::array<uint8_t,20> digest{};
            SHA1(item.bytes.data()+block*0x400, 0x400, digest.data());
            require(std::memcmp(digest.data(), hashes.data()+block*20, 20)==0,
                    "ISO cluster H0 mismatch (corrupt data or unsupported key)");
        }
        item.index = index; item.age = ++clock;
        counters.decryptHashMicros+=micros()-decryptStart;
        return item;
    }
    void read(uint64_t offset, void* destination, size_t length) {
        require(fits(offset, length, logicalSize), "Read outside decrypted Wii partition");
        require(destination || !length, "Null ISO read destination");
        counters.requestedBytes += length;
        auto* out = static_cast<uint8_t*>(destination);
        while (length) {
            const auto& item = cluster(offset/kPayload);
            const size_t bias = static_cast<size_t>(offset%kPayload);
            const size_t part = std::min(length, kPayload-bias);
            std::memcpy(out, item.bytes.data()+bias, part);
            length -= part; offset += part; out += part;
        }
    }
    void add(DiscEntry entry) {
        require(fits(entry.offset, entry.directory ? 0 : entry.size, logicalSize), "FST extent outside partition");
        const auto key = canonical(entry.path);
        require(paths.emplace(key, entries.size()).second, "Duplicate ISO file path");
        entries.push_back(std::move(entry));
    }
    void parseFst() {
        require(fst.size()>=12 && fst[0]==1 && be32(fst.data()+4)==0, "Invalid Wii FST root");
        const uint32_t count = be32(fst.data()+8);
        require(count>=1 && count<=65536 && uint64_t(count)*12<=fst.size(), "Invalid Wii FST count");
        const size_t names = size_t(count)*12;
        struct Dir { uint32_t index, end; std::string path; };
        std::vector<Dir> stack{{0,count,""}};
        for (uint32_t i=1; i<count; ++i) {
            while (!stack.empty() && i>=stack.back().end) stack.pop_back();
            require(!stack.empty(), "Invalid Wii FST directory nesting");
            const auto* e=fst.data()+size_t(i)*12;
            const uint32_t word=be32(e), name=word&0xffffff;
            require((word>>24)<=1 && name<fst.size()-names, "Invalid Wii FST entry");
            const char* begin=reinterpret_cast<const char*>(fst.data()+names+name);
            const char* end=static_cast<const char*>(std::memchr(begin, 0, fst.size()-names-name));
            require(end && end!=begin, "Unterminated Wii FST name");
            const std::string component(begin,end);
            require(component!="." && component!=".." && component.find_first_of("/\\")==std::string::npos,
                    "Invalid Wii FST path component");
            const std::string path=stack.back().path+"/"+component;
            if (word>>24) {
                const uint32_t next=be32(e+8);
                require(be32(e+4)==stack.back().index && next>i && next<=stack.back().end,
                        "Invalid Wii FST directory range");
                add({path,0,0,true}); stack.push_back({i,next,path});
            } else add({path,uint64_t(be32(e+4))*4,be32(e+8),false});
        }
    }
};

Disc::Disc(size_t cacheClusters) : impl_(std::make_unique<Impl>(cacheClusters)) {}
Disc::~Disc() = default;
void Disc::open(const std::string& path) {
    auto& s=*impl_; std::lock_guard<DiscMutex> lock(s.mutex);
    s.ready=false; s.id.clear(); s.entries.clear(); s.paths.clear(); s.fst.clear();
    s.clock=0; s.counters={}; s.logicalSize=0;
    for (auto& c:s.cache) { c.index=UINT64_MAX; c.age=0; }
    s.raw.open(path);
    std::array<uint8_t,0x100> header{}; s.rawRead(0,header.data(),header.size());
    require(be32(header.data()+0x18)==kWiiMagic, "Expected a raw Wii ISO image");
    require(std::memcmp(header.data(),"RMCP01",6)==0 && header[6]==0 && header[7]==0,
            "Expected clean Mario Kart Wii PAL RMCP01 revision 0");
    require(header[0x60]==0 && header[0x61]==0, "Expected a hashed, encrypted retail ISO");
    s.id.assign(reinterpret_cast<const char*>(header.data()),6);
    std::array<uint8_t,32> groups{}; s.rawRead(0x40000,groups.data(),groups.size());
    uint64_t partition=0;
    for (size_t g=0; g<4 && !partition; ++g) {
        const uint32_t count=be32(groups.data()+g*8);
        const uint64_t table=uint64_t(be32(groups.data()+g*8+4))*4;
        require(count<=64 && fits(table,uint64_t(count)*8,s.raw.size()), "Invalid Wii partition table");
        for (uint32_t i=0; i<count; ++i) {
            std::array<uint8_t,8> entry{}; s.rawRead(table+i*8,entry.data(),entry.size());
            if (be32(entry.data()+4)==0) { partition=uint64_t(be32(entry.data()))*4; break; }
        }
    }
    require(partition!=0, "Wii data partition missing");
    std::array<uint8_t,0x2c0> ticket{}; s.rawRead(partition,ticket.data(),ticket.size());
    require(ticket[0x1f1]==0, "Unsupported Wii common-key index");
    AES_KEY common{}; require(AES_set_decrypt_key(kRetailKey.data(),128,&common)==0, "AES initialization failed");
    std::array<uint8_t,16> iv{}, title{};
    std::memcpy(iv.data(),ticket.data()+0x1dc,8);
    AES_cbc_encrypt(ticket.data()+0x1bf,title.data(),title.size(),&common,iv.data(),AES_DECRYPT);
    require(AES_set_decrypt_key(title.data(),128,&s.titleKey)==0, "Title key initialization failed");
    const uint64_t relative=uint64_t(be32(ticket.data()+0x2b8))*4;
    const uint64_t physical=uint64_t(be32(ticket.data()+0x2bc))*4;
    require(relative>=ticket.size() && physical && physical%kCluster==0 &&
            fits(partition,relative,s.raw.size()) && fits(partition+relative,physical,s.raw.size()),
            "Invalid Wii partition data extent");
    s.dataStart=partition+relative; s.logicalSize=(physical/kCluster)*kPayload;
    std::array<uint8_t,0x440> boot{}; s.read(0,boot.data(),boot.size());
    require(std::memcmp(boot.data(),header.data(),8)==0 && be32(boot.data()+0x18)==kWiiMagic,
            "Wii partition does not match the disc header");
    const uint64_t fstOffset=uint64_t(be32(boot.data()+0x424))*4;
    const uint64_t fstSize=uint64_t(be32(boot.data()+0x428))*4;
    require(fstSize>=12 && fstSize<=8*1024*1024, "Invalid Wii FST size");
    s.fst.resize(static_cast<size_t>(fstSize)); s.read(fstOffset,s.fst.data(),s.fst.size()); s.parseFst();
    const uint64_t dolOffset=uint64_t(be32(boot.data()+0x420))*4;
    std::array<uint8_t,0x100> dol{}; s.read(dolOffset,dol.data(),dol.size());
    uint64_t dolSize=dol.size();
    for (size_t i=0;i<18;++i) if (const uint32_t size=be32(dol.data()+0x90+i*4))
        dolSize=std::max(dolSize,uint64_t(be32(dol.data()+i*4))+size);
    require(dolSize<=32*1024*1024, "Invalid DOL size");
    s.add({"/sys/main.dol",dolOffset,static_cast<uint32_t>(dolSize),false});
    s.add({"/sys/fst.bin",fstOffset,static_cast<uint32_t>(fstSize),false});
    s.add({"/sys/boot.bin",0,0x440,false}); s.add({"/sys/bi2.bin",0x440,0x2000,false});
    s.ready=true;
}
void Disc::read(uint64_t offset, void* out, size_t length) {
    auto& s=*impl_; std::lock_guard<DiscMutex> lock(s.mutex);
    require(s.ready,"ISO is not mounted"); s.read(offset,out,length);
}
void Disc::readRaw(uint64_t offset,void* out,size_t length) {
    auto& s=*impl_;std::lock_guard<DiscMutex> lock(s.mutex);
    require(s.ready,"ISO is not mounted");s.rawRead(offset,out,length);
}
void Disc::readFile(const std::string& path,uint64_t offset,void* out,size_t length) {
    auto& s=*impl_; std::lock_guard<DiscMutex> lock(s.mutex);
    require(s.ready,"ISO is not mounted"); const auto it=s.paths.find(canonical(path));
    require(it!=s.paths.end(),"File absent from ISO"); const auto& e=s.entries[it->second];
    require(!e.directory && fits(offset,length,e.size),"Read outside ISO file"); s.read(e.offset+offset,out,length);
}
std::vector<uint8_t> Disc::file(const std::string& path,size_t maxSize) {
    const auto* e=find(path); require(e && !e->directory,"File absent from ISO");
    require(e->size<=maxSize,"ISO file exceeds the caller's memory budget");
    std::vector<uint8_t> bytes(e->size); readFile(path,0,bytes.data(),bytes.size()); return bytes;
}
const DiscEntry* Disc::find(const std::string& path) const {
    const auto& s=*impl_; require(s.ready,"ISO is not mounted"); const auto it=s.paths.find(canonical(path));
    return it==s.paths.end()?nullptr:&s.entries[it->second];
}
const std::vector<DiscEntry>& Disc::entries() const { require(impl_->ready,"ISO is not mounted"); return impl_->entries; }
const std::vector<uint8_t>& Disc::fst() const { require(impl_->ready,"ISO is not mounted"); return impl_->fst; }
const std::string& Disc::gameId() const { return impl_->id; }
uint64_t Disc::isoSize() const { return impl_->raw.size(); }
uint64_t Disc::partitionSize() const { return impl_->logicalSize; }
DiscStats Disc::stats() const { std::lock_guard<DiscMutex> lock(impl_->mutex); return impl_->counters; }
} // namespace wiicompiled
