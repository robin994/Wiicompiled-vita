#pragma once

namespace wiicompiled::vita {
// Execute once on the original thread stack, before loading the game.
// A failed prerequisite aborts boot with a diagnostic instead of running PPC.
void CheckRuntimeContracts();
}
