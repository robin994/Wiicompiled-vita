#include "gx.hpp"

#if defined(MKW_TARGET_VITA)
#include "../../vita/gfx_frontend.hpp"
#else
#include "../../gfx/depth_peek.hpp"
#endif

#include <dolphin/gx/GXCpu2Efb.h>

void GXPeekZ(u16 x, u16 y, u32* z) {
  aurora::gfx::depth_peek::poll();

  if (z != nullptr) {
    u32 value = 0;
    if (aurora::gfx::depth_peek::read_latest(x, y, value)) {
      *z = value;
    } else {
      *z = g_gxState.clearDepth & 0x00ffffffu;
    }
  }

  aurora::gfx::depth_peek::request_snapshot();
}
