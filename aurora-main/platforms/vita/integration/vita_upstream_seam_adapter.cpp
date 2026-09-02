#include "vita_upstream_seam_adapter.hpp"
#if defined(AURORA_VITA_UPSTREAM_SEAM)
namespace aurora::vita::integration {
aurora::gx::backend_seam::SubmitStatus VitaUpstreamSeamAdapter::submit(const aurora::gx::backend_seam::RawDraw& draw) noexcept {
  GxRawDraw d{};
  d.primitive = draw.primitive;
  d.vertexFormat = draw.vertexFormat;
  d.vertexData = draw.vertexData;
  d.vertexBytes = draw.vertexBytes;
  d.vertexCount = draw.vertexCount;
  d.indexData = draw.indexData;
  d.indexCount = draw.indexCount;
  const auto s = backend_.submit_raw_draw(d);
  return {s.ok, s.error, s.warningMask};
}
} // namespace aurora::vita::integration
#endif
