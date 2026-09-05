#pragma once

namespace WiiCompiledVita {
// Replacing the immediately preceding sampled copy is legal only if no draw
// can observe it and it did not clear the framebuffer. Preserve destroy->copy
// because that transition starts a new resource lifetime.
constexpr bool CanReplaceEfbCommand(bool sameBoundary, bool sameDestination,
                                    bool previousClears, bool previousCopy,
                                    bool incomingDestroy) noexcept {
    return sameBoundary && sameDestination && !previousClears &&
           (previousCopy || incomingDestroy);
}
} // namespace WiiCompiledVita
