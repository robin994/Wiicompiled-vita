#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>
namespace wiicompiled::vita::assets {
// Initialize after mounting the PSARC and before entering the guest. A missing
// catalogue keeps the original asset path available for the control build.
bool Initialize(size_t cacheBudget=2*1024*1024) noexcept;
struct PrewarmStats {
    bool manifestLoaded=false;
    uint32_t selected=0,loaded=0,alreadyCached=0,rejected=0;
    uint32_t remaining=0,skippedLarge=0;
    size_t bytes=0;
};
// Optional CPU-only sidecar prewarm. The manifest records actual accesses
// from a previous visit to the scene; no GXM/GPU resource may be created here.
// The budget is additionally clamped to the existing asset cache budget.
bool Initialize(size_t cacheBudget,size_t prewarmBudget,
                const char* manifestRoot) noexcept;
// Must be called AFTER Aurora shader/program/pipeline prewarm: shader reads
// share the native sidecar LRU and would evict boot-prefetched records.
PrewarmStats WarmBoot() noexcept;
PrewarmStats EnterScene(uint64_t stableSceneKey) noexcept;
// Non-blocking *planning* of scene prewarm. Does not read sidecar contents.
// Use StepScenePrewarm after a presented frame, with a small per-tick budget.
PrewarmStats QueueScene(uint64_t stableSceneKey) noexcept;
PrewarmStats StepScenePrewarm(size_t maxBytes=64*1024,unsigned maxAssets=1) noexcept;
bool Checkpoint() noexcept;
bool PrewarmEnabled() noexcept;
bool Read(const char* path, std::vector<uint8_t>& bytes, void* context) noexcept;
void Shutdown() noexcept;
}
