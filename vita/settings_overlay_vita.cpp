#include "settings_overlay.h"

namespace settings_overlay {

void InitializeRuntimeSettings() noexcept {}
void HandleEvents(const AuroraEvent*) noexcept {}
void Draw() noexcept {}
bool StartupScreenVisible() noexcept { return false; }
void NotifyStrapInputAccepted() noexcept {}
void AdvancePresentedFrame() noexcept {}

} // namespace settings_overlay
