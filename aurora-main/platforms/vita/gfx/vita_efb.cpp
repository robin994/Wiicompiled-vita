#include "vita_efb.hpp"
#include "vita_gl_util.hpp"
#include <algorithm>
#include <cstddef>
#if defined(__vita__)
#include <vitaGL.h>
#endif

namespace aurora::vita::gfx {
namespace {
#if defined(__vita__)
GLint wrap_mode(WrapMode w) noexcept {
  switch (w) {
  case WrapMode::Clamp: return GL_CLAMP_TO_EDGE;
  case WrapMode::Repeat: return GL_REPEAT;
  case WrapMode::Mirror: return GL_MIRRORED_REPEAT;
  }
  return GL_CLAMP_TO_EDGE;
}

GLint filter_mode(Filter f) noexcept {
  switch (f) {
  case Filter::Nearest:
  case Filter::NearestMipmapNearest:
  case Filter::NearestMipmapLinear:
    return GL_NEAREST;
  case Filter::Linear:
  case Filter::LinearMipmapNearest:
  case Filter::LinearMipmapLinear:
    return GL_LINEAR;
  }
  return GL_LINEAR;
}

constexpr const char* BlitVs =
    "precision highp float;"
    "attribute vec2 a_position; attribute vec2 a_tex0; varying vec2 v;"
    "void main(){gl_Position=vec4(a_position,0.0,1.0);v=a_tex0;}";

constexpr const char* FragPassthrough =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "void main(){gl_FragColor=texture2D(u_tex0,v);}";
constexpr const char* FragI4 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "float i3(vec3 c){return dot(c,vec3(0.257,0.504,0.098))+16.0/255.0;}"
    "float q4(float x){return floor(x*16.0)/15.0;}"
    "void main(){float i=q4(i3(texture2D(u_tex0,v).rgb));gl_FragColor=vec4(i);}";
constexpr const char* FragI8 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "float i3(vec3 c){return dot(c,vec3(0.257,0.504,0.098))+16.0/255.0;}"
    "void main(){float i=i3(texture2D(u_tex0,v).rgb);gl_FragColor=vec4(i);}";
constexpr const char* FragIA4 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "float i3(vec3 c){return dot(c,vec3(0.257,0.504,0.098))+16.0/255.0;}"
    "float q4(float x){return floor(x*16.0)/15.0;}"
    "void main(){vec4 c=texture2D(u_tex0,v);float i=q4(i3(c.rgb));gl_FragColor=vec4(i,i,i,q4(c.a));}";
constexpr const char* FragIA8 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "float i3(vec3 c){return dot(c,vec3(0.257,0.504,0.098))+16.0/255.0;}"
    "void main(){vec4 c=texture2D(u_tex0,v);float i=i3(c.rgb);gl_FragColor=vec4(i,i,i,c.a);}";
constexpr const char* FragRGB565 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "void main(){vec4 c=texture2D(u_tex0,v);gl_FragColor=vec4(c.rgb,1.0);}";
constexpr const char* FragR4 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "float q4(float x){return floor(x*16.0)/15.0;}"
    "void main(){float r=q4(texture2D(u_tex0,v).r);gl_FragColor=vec4(r);}";
constexpr const char* FragRA4 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "float q4(float x){return floor(x*16.0)/15.0;}"
    "void main(){vec4 c=texture2D(u_tex0,v);float r=q4(c.r);gl_FragColor=vec4(r,r,r,q4(c.a));}";
constexpr const char* FragRA8 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "void main(){vec4 c=texture2D(u_tex0,v);gl_FragColor=vec4(c.r,c.r,c.r,c.a);}";
constexpr const char* FragA8 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "void main(){float a=texture2D(u_tex0,v).a;gl_FragColor=vec4(a);}";
constexpr const char* FragR8 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "void main(){float r=texture2D(u_tex0,v).r;gl_FragColor=vec4(r);}";
constexpr const char* FragG8 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "void main(){float g=texture2D(u_tex0,v).g;gl_FragColor=vec4(g);}";
constexpr const char* FragB8 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "void main(){float b=texture2D(u_tex0,v).b;gl_FragColor=vec4(b);}";
constexpr const char* FragRG8 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "void main(){vec4 c=texture2D(u_tex0,v);gl_FragColor=vec4(c.r,c.r,c.r,c.g);}";
constexpr const char* FragGB8 =
    "precision highp float; uniform sampler2D u_tex0; varying vec2 v;"
    "void main(){vec4 c=texture2D(u_tex0,v);gl_FragColor=vec4(c.g,c.g,c.g,c.b);}";

const char* copy_fragment(EfbCopyFormat format) noexcept {
  switch (format) {
  case EfbCopyFormat::Passthrough: return FragPassthrough;
  case EfbCopyFormat::I4: return FragI4;
  case EfbCopyFormat::I8: return FragI8;
  case EfbCopyFormat::IA4: return FragIA4;
  case EfbCopyFormat::IA8: return FragIA8;
  case EfbCopyFormat::RGB565: return FragRGB565;
  case EfbCopyFormat::R4: return FragR4;
  case EfbCopyFormat::RA4: return FragRA4;
  case EfbCopyFormat::RA8: return FragRA8;
  case EfbCopyFormat::A8: return FragA8;
  case EfbCopyFormat::R8: return FragR8;
  case EfbCopyFormat::G8: return FragG8;
  case EfbCopyFormat::B8: return FragB8;
  case EfbCopyFormat::RG8: return FragRG8;
  case EfbCopyFormat::GB8: return FragGB8;
  default: return nullptr;
  }
}
#endif
} // namespace

EfbManager::~EfbManager() { clear(); }

Handle EfbManager::create(uint32_t w, uint32_t h, bool depth) noexcept {
  if (!w || !h) return InvalidHandle;
  Entry e{};
  e.width = w;
  e.height = h;
  e.bytes = static_cast<size_t>(w) * h * 4u * (depth ? 2u : 1u);
#if defined(__vita__)
  const GLuint previousFbo = boundFbo_;
  GLuint fbo = 0, tex = 0, rb = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // vitaGL speedhack's RGBA upload callback dereferences the source pointer even for
  // storage-only render textures. Seed the allocation with real zeroed pixels instead
  // of glTexImage2D(..., nullptr); this is paid only when an EFB destination is created.
  std::vector<uint8_t> initialPixels;
  try {
    initialPixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u, 0u);
  } catch (...) {
    if (tex) glDeleteTextures(1, &tex);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
    return InvalidHandle;
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, initialPixels.data());
  // A failed speedhack upload does not reliably set a GL error. Never attach a
  // texture slot that has no backing store: the caller can skip this GXCopyTex
  // instead of submitting an invalid GXM texture.
  if (vglGetTexDataPointer(GL_TEXTURE_2D) == nullptr) {
    if (tex) glDeleteTextures(1, &tex);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
    return InvalidHandle;
  }
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  if (depth) {
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
  }
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    if (rb) glDeleteRenderbuffers(1, &rb);
    if (tex) glDeleteTextures(1, &tex);
    if (fbo) glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
    return InvalidHandle;
  }
  e.fbo = fbo;
  e.color = tex;
  e.depth = rb;
  glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
#else
  (void)depth;
  e.fbo = next_;
  e.color = next_;
#endif
  const Handle handle = next_++;
  map_[handle] = e;
  bytes_ += e.bytes;
  highWaterBytes_ = std::max(highWaterBytes_, bytes_);
  return handle;
}

bool EfbManager::bind(Handle h) noexcept {
  const auto it = map_.find(h);
  if (it == map_.end()) return false;
#if defined(__vita__)
  glBindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
  boundFbo_ = it->second.fbo;
  glViewport(0, 0, it->second.width, it->second.height);
#endif
  return true;
}

void EfbManager::bind_default(uint32_t w, uint32_t h) noexcept {
#if defined(__vita__)
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  boundFbo_ = 0;
  glViewport(0, 0, w, h);
#else
  (void)w;
  (void)h;
#endif
}

bool EfbManager::ensure_blitter(EfbCopyFormat format) noexcept {
  if (is_depth_copy_format(format)) return false;
#if defined(__vita__)
  const size_t idx = static_cast<size_t>(format);
  if (idx >= CopyProgramCount) return false;
  if (!blitPrograms_[idx]) {
    const char* fs = copy_fragment(format);
    if (!fs) return false;
    blitPrograms_[idx] = link_program(BlitVs, fs);
    if (!blitPrograms_[idx]) return false;
    blitTex_[idx] = glGetUniformLocation(blitPrograms_[idx], "u_tex0");
  }
  if (!blitVbo_) {
    const float q[] = {-1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1};
    glGenBuffers(1, &blitVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, blitVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
  }
#else
  (void)format;
#endif
  return true;
}

bool EfbManager::draw_texture(unsigned texture, uint32_t width, uint32_t height, EfbCopyFormat format) noexcept {
  if (!ensure_blitter(format)) return false;
#if defined(__vita__)
  const size_t idx = static_cast<size_t>(format);
  glViewport(0, 0, width, height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glUseProgram(blitPrograms_[idx]);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (blitTex_[idx] >= 0) glUniform1i(blitTex_[idx], 0);
  glBindBuffer(GL_ARRAY_BUFFER, blitVbo_);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void*)0);
  glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (const void*)(2 * sizeof(float)));
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(3);
#else
  (void)texture;
  (void)width;
  (void)height;
#endif
  return true;
}

bool EfbManager::blit_to_default(Handle h, uint32_t w, uint32_t he) noexcept {
  const auto it = map_.find(h);
  if (it == map_.end()) return false;
  bind_default(w, he);
  return draw_texture(it->second.color, w, he, EfbCopyFormat::Passthrough);
}

Handle EfbManager::capture_from_bound(Handle existing, int32_t srcX, int32_t srcY, uint32_t srcWidth,
                                      uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight,
                                      EfbCopyFormat format) noexcept {
  if (!srcWidth || !srcHeight || !dstWidth || !dstHeight || is_depth_copy_format(format)) return InvalidHandle;
  Handle dst = existing;
  uint32_t ew = 0, eh = 0;
  if (dst && (!dimensions(dst, ew, eh) || ew != dstWidth || eh != dstHeight)) {
    destroy(dst);
    dst = InvalidHandle;
  }
  if (!dst) dst = create(dstWidth, dstHeight, false);
  if (!dst) return InvalidHandle;
#if defined(__vita__)
  const GLuint sourceFbo = boundFbo_;
  const auto dstIt = map_.find(dst);
  if (dstIt == map_.end() || !dstIt->second.fbo) {
    destroy(dst);
    return InvalidHandle;
  }

  // GPU-only GXCopyTex path. glBlitFramebuffer in vitaGL uses the GXM blit shader and
  // supports scaling, so no glReadPixels/sceGxmTransferFinish round-trip is needed.
  //
  // M12.6 crashed in vitaGL's update_scissor_test while glBlitFramebuffer was
  // switching to the destination FBO. The blit is a copy operation and must not
  // inherit the draw scissor anyway, so disable it across the FBO transition and
  // restore it only after the original render target is rebound. This keeps
  // vitaGL from rebuilding a scissor mask against the transient blit target.
  const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
  if (scissorWasEnabled) glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstIt->second.fbo);
  while (glGetError() != GL_NO_ERROR) {}
  glBlitFramebuffer(srcX, srcY, srcX + static_cast<GLint>(srcWidth),
                    srcY + static_cast<GLint>(srcHeight),
                    0, 0, static_cast<GLint>(dstWidth), static_cast<GLint>(dstHeight),
                    GL_COLOR_BUFFER_BIT, GL_LINEAR);
  const GLenum blitError = glGetError();
  glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sourceFbo);
  boundFbo_ = sourceFbo;
  if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
  if (blitError != GL_NO_ERROR) {
    destroy(dst);
    return InvalidHandle;
  }
#else
  (void)srcX;
  (void)srcY;
  (void)format;
#endif
  return dst;
}

Handle EfbManager::upload_rgba(Handle existing, uint32_t w, uint32_t h, const void* rgba) noexcept {
  if (!w || !h || !rgba) return InvalidHandle;
  if (w > 2048u || h > 2048u || static_cast<size_t>(w) > SIZE_MAX / (static_cast<size_t>(h) * 4u)) {
    return InvalidHandle;
  }

  Handle handle = existing;
  auto it = handle ? map_.find(handle) : map_.end();
  if (it != map_.end() && (it->second.width != w || it->second.height != h || it->second.fbo != 0)) {
    destroy(handle);
    handle = InvalidHandle;
    it = map_.end();
  }

#if defined(__vita__)
  if (it != map_.end()) {
    glBindTexture(GL_TEXTURE_2D, it->second.color);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    if (glGetError() != GL_NO_ERROR) return InvalidHandle;
    return handle;
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  if (!tex) return InvalidHandle;
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // vitaGL speedhack can fault while allocating an empty RGBA render texture through
  // glTexImage2D(..., nullptr). Supplying the already-read pixels takes the normal,
  // hardware-tested texture upload path and removes the extra FBO/capture allocation.
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  if (vglGetTexDataPointer(GL_TEXTURE_2D) == nullptr || glGetError() != GL_NO_ERROR) {
    glDeleteTextures(1, &tex);
    return InvalidHandle;
  }
#else
  if (it != map_.end()) return handle;
  const unsigned tex = next_;
#endif

  Entry e{};
  e.fbo = 0;
  e.color = tex;
  e.depth = 0;
  e.width = w;
  e.height = h;
  e.bytes = static_cast<size_t>(w) * h * 4u;
  handle = next_++;
  map_[handle] = e;
  bytes_ += e.bytes;
  highWaterBytes_ = std::max(highWaterBytes_, bytes_);
  return handle;
}

bool EfbManager::bind_texture(Handle h, unsigned unit, const SamplerDesc& s) noexcept {
  const auto it = map_.find(h);
  if (it == map_.end()) return false;
#if defined(__vita__)
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, it->second.color);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode(s.wrapS));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode(s.wrapT));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_mode(s.minFilter));
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_mode(s.magFilter));
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, s.lodBias);
#else
  (void)unit;
  (void)s;
#endif
  return true;
}

bool EfbManager::read_rgba(Handle h, std::vector<uint8_t>& out) noexcept {
  const auto it = map_.find(h);
  if (it == map_.end()) return false;
  out.resize(static_cast<size_t>(it->second.width) * it->second.height * 4);
#if defined(__vita__)
  const GLuint previousFbo = boundFbo_;
  glBindFramebuffer(GL_FRAMEBUFFER, it->second.fbo);
  glReadPixels(0, 0, it->second.width, it->second.height, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
  glBindFramebuffer(GL_FRAMEBUFFER, previousFbo);
#endif
  return true;
}

bool EfbManager::dimensions(Handle h, uint32_t& w, uint32_t& hgt) const noexcept {
  const auto it = map_.find(h);
  if (it == map_.end()) return false;
  w = it->second.width;
  hgt = it->second.height;
  return true;
}

void EfbManager::destroy(Handle h) noexcept {
  const auto it = map_.find(h);
  if (it == map_.end()) return;
#if defined(__vita__)
  GLuint depth = it->second.depth, color = it->second.color, fbo = it->second.fbo;
  if (fbo != 0 && boundFbo_ == fbo) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    boundFbo_ = 0;
  }
  if (depth) glDeleteRenderbuffers(1, &depth);
  if (color) glDeleteTextures(1, &color);
  if (fbo) glDeleteFramebuffers(1, &fbo);
#endif
  bytes_ -= it->second.bytes;
  map_.erase(it);
}

void EfbManager::clear() noexcept {
  while (!map_.empty()) destroy(map_.begin()->first);
#if defined(__vita__)
  if (blitVbo_) {
    GLuint b = blitVbo_;
    glDeleteBuffers(1, &b);
  }
  for (auto& program : blitPrograms_) {
    if (program) glDeleteProgram(program);
  }
#endif
  blitVbo_ = 0;
  blitPrograms_.fill(0);
  blitTex_.fill(-1);
  boundFbo_ = 0;
  bytes_ = 0;
}

#if defined(__vita__)
unsigned EfbManager::color_texture(Handle h) const noexcept {
  const auto it = map_.find(h);
  return it == map_.end() ? 0 : it->second.color;
}
#endif

} // namespace aurora::vita::gfx
