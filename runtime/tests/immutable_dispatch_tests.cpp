#include "abi_bridge.h"

#include <atomic>
#include <cstdlib>
#include <thread>
#include <vector>

void ShowRuntimeFatalPopup(std::string_view, std::string_view) noexcept { std::abort(); }

namespace {
void Check(bool value) { if (!value) std::abort(); }
void Base(CpuContext*) {}
void Native(CpuContext*) {}
constexpr uint32_t target = 0x81234568u;
constexpr uint32_t other = target + 4 * kIndirectDispatchCacheEntries;
constexpr uint32_t absent = target + 8 * kIndirectDispatchCacheEntries;
constexpr uint32_t late = 0x81238000u;
constexpr uint32_t lateBase = 0x81238004u;
void Register(uint32_t address, FunctionKind kind, void (*entry)(CpuContext*)) {
    TranslatedFunctionInfo info{};
    info.address = address;
    info.name = "dispatch-test";
    info.kind = kind;
    info.entryPoint = reinterpret_cast<void*>(entry);
    info.rawCpuInvoker = entry;
    info.nonvolatileFprWriteMask = 0;
    TranslatedFunctionRegistry::Register(info);
}
void VerifyPublished() {
    Check(!IsBaseTranslatedCpuTargetActive<target>());
    Check(IsBaseTranslatedCpuTargetActive<other>());
    Check(!IsBaseTranslatedCpuTargetActive<absent>());
    Check(!IsBaseTranslatedCpuTargetActive<late>());
    Check(IsBaseTranslatedCpuTargetActive<lateBase>());
    // These addresses collide in both memos. Negative hits must not replace
    // another target's positive hit, and each thread must acquire on first use.
    for (int i = 0; i < 2000; ++i) {
        const auto* winner = TranslatedFunctionRegistry::FindByAddressPtr(target);
        Check(winner && winner->kind == FunctionKind::Native && winner->rawCpuInvoker == Native);
        Check(TranslatedFunctionRegistry::FindByAddressPtr(target) == winner);
        const auto* raw = TranslatedFunctionRegistry::FindRawByAddressPtr(target);
        Check(raw && raw->entry == Native && raw->preserveNonvolatileGprs);
        Check(TranslatedFunctionRegistry::FindRawByAddressPtr(target) == raw);
        Check(TranslatedFunctionRegistry::FindByAddressPtr(other)->rawCpuInvoker == Base);
        Check(TranslatedFunctionRegistry::FindRawByAddressPtr(other)->entry == Base);
        Check(!TranslatedFunctionRegistry::FindByAddressPtr(absent));
        Check(!TranslatedFunctionRegistry::FindByAddressPtr(absent));
        Check(!TranslatedFunctionRegistry::FindRawByAddressPtr(absent));
        Check(!TranslatedFunctionRegistry::FindRawByAddressPtr(absent));
    }
}
} // namespace

int main(int argc, char**) {
    Register(target, FunctionKind::BaseTranslated, Base);
    Check(IsBaseTranslatedCpuTargetActive<target>());
    Check(!IsBaseTranslatedCpuTargetActive<late>());
    Check(!IsBaseTranslatedCpuTargetActive<lateBase>());
    Check(TranslatedFunctionRegistry::FindByAddressPtr(target)->rawCpuInvoker == Base);
    Check(!TranslatedFunctionRegistry::FindByAddressPtr(late));
    Check(!TranslatedFunctionRegistry::FindRawByAddressPtr(target));
    for (const auto& memo : g_indirectResolvedDispatchMemo) Check(!memo.valid);
    for (const auto& memo : g_indirectRawDispatchMemo) Check(!memo.valid);
    Register(target, FunctionKind::Native, Native);
    Register(other, FunctionKind::BaseTranslated, Base);
    Register(late, FunctionKind::Native, Native);
    Register(lateBase, FunctionKind::BaseTranslated, Base);
    Check(!IsBaseTranslatedCpuTargetActive<target>());
    Check(TranslatedFunctionRegistry::FindByAddressPtr(target)->rawCpuInvoker == Native);

    std::array<RawDispatchRecord, 2> records{{
        {other, Base, 0, false}, {lateBase, Base, 0, false}}};
    constexpr uint16_t firstPage = (other >> 12) & 0xfffu;
    constexpr size_t pageCount = ((lateBase >> 12) & 0xfffu) - firstPage + 1;
    std::array<StaticIndirectDispatchPage, pageCount> pages{};
    pages.front() = {0, 1, 0};
    pages.back() = {1, 1, 0};
    std::array<StaticIndirectDispatchSegment, 256> segments{};
    segments[other >> 24] = {pages.data(), firstPage, uint16_t(pageCount)};
    StaticIndirectDispatchTable table{"test", segments.data(), records.data(), records.size()};
    if (argc > 1) RegisterStaticIndirectDispatchTable(&table);

    // Threads exist before publication. Their first successful publication
    // acquire, including the payload visibility it guarantees, happens inside
    // the production registry API rather than through a separate test signal.
    std::vector<std::thread> readers;
    for (int i = 0; i < 6; ++i) readers.emplace_back([] {
        while (!TranslatedFunctionRegistry::IsLookupPublished()) std::this_thread::yield();
        VerifyPublished();
    });
    TranslatedFunctionRegistry::Finalize();
    TranslatedFunctionRegistry::Finalize();
    VerifyPublished();
    for (auto& reader : readers) reader.join();
    std::puts("PASS: provisional winners, negative hits, colliding memos and concurrent immutable publication.");
}
