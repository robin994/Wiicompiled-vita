#pragma once
#include "vita_gx_backend.hpp"
#if defined(AURORA_VITA_UPSTREAM_SEAM)
#if defined(AURORA_VITA_UPSTREAM_SEAM_STUB)
#include "../../../patches/upstream_backend_seam/backend_seam.hpp"
#else
#include "../../../lib/gx/backend_seam.hpp"
#endif
namespace aurora::vita::integration {
class VitaUpstreamSeamAdapter final : public aurora::gx::backend_seam::Backend {
public:
  explicit VitaUpstreamSeamAdapter(GxBackendApi& backend) noexcept : backend_(backend) {}
  aurora::gx::backend_seam::SubmitStatus submit(const aurora::gx::backend_seam::RawDraw& draw) noexcept override;
  bool copy_tex(const void* destination, bool clear) noexcept override { return backend_.copy_tex(destination, clear); }
  void evict_copy_tex(const void* destination) noexcept override { backend_.evict_copy_tex(destination); }
  void clear_copy_textures() noexcept override { backend_.clear_copy_textures(); }
private:
  GxBackendApi& backend_;
};
} // namespace aurora::vita::integration
#endif
