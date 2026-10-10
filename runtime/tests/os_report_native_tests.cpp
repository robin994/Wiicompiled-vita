#include "abi_bridge.h"
#include "host_context.h"
#include "system_bridge.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <streambuf>
#include <string>

extern "C" void OS__Report_801a25d0(CpuContext*);

namespace {
constexpr uint32_t kBase = 0x80000000;
std::array<unsigned char, 32768> g_memory{};
HostContext::Handle g_scheduler = nullptr;
HostContext::Handle g_worker = nullptr;
CpuContext g_cpu{};
int g_dumps = 0;
int g_steps = 0;

void RequireNative()
{
    if (g_scheduler && !HostContext::IsCurrent(g_scheduler)) std::abort();
}

// Fail if the real OSReport path reaches its output/flush seam on the worker
// stack. Back it with a real FILE so the test also checks the emitted bytes.
class NativeOutput : public std::streambuf {
public:
    FILE* file = std::tmpfile();
    ~NativeOutput() override { if (file) std::fclose(file); }
    std::streamsize xsputn(const char* text, std::streamsize size) override {
        RequireNative();
        return std::fwrite(text, 1, static_cast<size_t>(size), file);
    }
    int_type overflow(int_type ch) override {
        RequireNative();
        if (traits_type::eq_int_type(ch, traits_type::eof())) return traits_type::not_eof(ch);
        return std::fputc(traits_type::to_char_type(ch), file) == EOF ? traits_type::eof() : ch;
    }
    int sync() override { RequireNative(); return std::fflush(file); }
    std::string Read() {
        RequireNative();
        std::fflush(file);
        std::rewind(file);
        std::string result;
        std::array<char, 512> block;
        while (size_t bytes = std::fread(block.data(), 1, block.size(), file))
            result.append(block.data(), bytes);
        return result;
    }
};

void PutString(uint32_t address, const std::string& text)
{
    const size_t offset = address - kBase;
    if (offset + text.size() >= g_memory.size()) std::abort();
    std::memcpy(g_memory.data() + offset, text.c_str(), text.size() + 1);
}

void Report(const std::string& format)
{
    PutString(kBase + 32, format);
    g_cpu.gpr[3] = kBase + 32;
    const auto savedGpr = g_cpu.gpr;
    const auto savedLr = g_cpu.lr;
    const double savedFp = g_cpu.fpr[1].d;
    OS__Report_801a25d0(&g_cpu);
    if (!HostContext::IsCurrent(g_worker) || g_cpu.gpr != savedGpr ||
        g_cpu.lr != savedLr || g_cpu.fpr[1].d != savedFp) std::abort();
}

void Worker(void*)
{
    g_cpu.lr = 0x8000a478;
    g_cpu.gpr[4] = 1;
    g_cpu.gpr[5] = 0x1234abcd;
    g_cpu.gpr[6] = kBase + 1024;
    g_cpu.fpr[1].d = 1.5;
    PutString(kBase + 1024, "guest text");
    Report("module=%d value=%08x text=%s number=%.2f\n");
    Report("module=%d value=%08x text=%s number=%.2f\n");
    Report("\n"); // A blank between repetitions must remain suppressed.
    g_cpu.gpr[4] = 0; // The 64-bit argument skips r4 for PPC alignment.
    g_cpu.gpr[5] = 1;
    g_cpu.gpr[6] = 2;
    g_cpu.gpr[7] = 9;
    Report("wide=%llu last=%d\n");
    g_cpu.gpr[1] = kBase + 8192;
    for (unsigned i = 0; i < 7; ++i) g_cpu.gpr[4 + i] = i + 1;
    g_memory[8192 + 11] = 8;
    Report("i=%d %d %d %d %d %d %d %d\n");
    PutString(kBase + 1024, std::string(2048, 'x'));
    g_cpu.gpr[4] = kBase + 1024;
    Report("%s\n");
    Report("file:42 Warning: native logging\n");
    Report("file:42 Warning: native logging\n");
    ++g_steps;
    HostContext::Switch(g_scheduler);
    Report("resumed\n");
    OS__Report_801a25d0(nullptr);
    if (!HostContext::IsCurrent(g_worker)) std::abort();
    ++g_steps;
    HostContext::Switch(g_scheduler);
}
} // namespace

// Only memory and diagnostic dependencies are replaced. Compile the real HLE
// entry and shared formatter with the Vita path enabled, plus the real context
// switch, so moving just the final write would still fail on guest string reads.
namespace GuestFlat { wiicompiled::RamMirrors g_vitaRamMirrors{}; }
namespace MemoryInline {
PageEntry g_pageTable[kPageCount]{};
uintptr_t g_fullPageBias[kPageCount]{};
uintptr_t g_fullReadablePageBias[kPageCount]{};
uintptr_t g_fullWritablePageBias[kPageCount]{};
const SparseWritablePageTable* g_sparseWritablePageTables[kPageCount]{};
uint8_t g_deferredReadCoveredPages[kPageCount]{};

uint8_t Read8Slow(uint32_t address)
{
    RequireNative();
    if (address < kBase || address - kBase >= g_memory.size()) std::abort();
    return g_memory[address - kBase];
}
uint32_t Read32Slow(uint32_t address)
{
    return (uint32_t(Read8Slow(address)) << 24) | (uint32_t(Read8Slow(address + 1)) << 16) |
           (uint32_t(Read8Slow(address + 2)) << 8) | Read8Slow(address + 3);
}
void Write32Slow(uint32_t, uint32_t) { std::abort(); }
} // namespace MemoryInline
namespace RecompMod {
std::atomic<uint8_t> g_executableWriteGuardPages[kExecutableWriteGuardPageCount]{};
}
uint8_t* Memory::GetPointer(uint32_t, size_t) { std::abort(); }
Memory::AccessViolation::AccessViolation(uint32_t address, size_t length, std::string_view reason)
    : std::runtime_error(std::string(reason)), address_(address), length_(length), reason_(reason) {}
CpuContext& GetPersistentCpuContext() { return g_cpu; }
void TranslatedFunctionRegistry::Register(TranslatedFunctionInfo) {}
void SystemBridge::DumpCpuState(const CpuContext* cpu)
{
    RequireNative();
    if (cpu != &g_cpu) std::abort();
    ++g_dumps;
    std::cerr << "context captured\n" << std::flush;
}

int main()
{
    NativeOutput output, errors;
    if (!output.file || !errors.file) return 1;
    auto* oldOut = std::cout.rdbuf(&output);
    auto* oldErr = std::cerr.rdbuf(&errors);
    // Calls on ordinary native threads remain valid before initialization.
    OS__Report_801a25d0(nullptr);
    if (!HostContext::InitializeScheduler(&g_scheduler)) return 1;
    g_worker = HostContext::Create(64 * 1024, Worker, nullptr);
    if (!g_worker) return 1;
    HostContext::Switch(g_worker);
    if (g_steps != 1 || !HostContext::IsCurrent(g_scheduler)) std::abort();
    HostContext::Switch(g_worker);
    if (g_steps != 2 || !HostContext::IsCurrent(g_scheduler) || g_dumps != 1) std::abort();
    const std::string expected =
        "[OSReport] (null fmt)\n"
        "[OSReport] module=1 value=1234abcd text=guest text number=1.50\n"
        "[OSReport] previous message repeated 1 time(s)\n"
        "[OSReport] wide=4294967298 last=9\n"
        "[OSReport] i=1 2 3 4 5 6 7 8\n"
        "[OSReport] " + std::string(2048, 'x') + "\n"
        "[OSReport] file:42 Warning: native logging\n"
        "[OSReport] previous message repeated 1 time(s)\n"
        "[OSReport] resumed\n"
        "[OSReport] (null fmt)\n";
    if (output.Read() != expected || errors.Read() != "context captured\n") std::abort();
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldErr);
    HostContext::Destroy(g_worker);
    HostContext::ShutdownScheduler(g_scheduler);
    g_scheduler = nullptr;
}
