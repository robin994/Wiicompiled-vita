// Internal GX -> vitaGL EFB implementation. Included after the GX packet types.
// All resources belong to USER_1; no Aurora renderer or transient FBO is used.
struct DirectEfbEntry {
    uintptr_t destination = 0;
    GLuint texture = 0;
    uint32_t width = 0, height = 0;
    size_t bytes = 0;
};
std::array<DirectEfbEntry, 128> g_directEfb{};
size_t g_directEfbBytes = 0;
constexpr size_t kDirectEfbBudget = 8 * 1024 * 1024;

DirectEfbEntry* FindDirectEfb(uintptr_t destination) {
    for (auto& entry : g_directEfb)
        if (entry.destination == destination && destination) return &entry;
    return nullptr;
}
void DestroyDirectEfbInternal(uintptr_t destination, bool gpuAlreadySynchronized) {
    if (auto* entry = FindDirectEfb(destination)) {
        if (!gpuAlreadySynchronized) glFinish(); // the previous draw may still sample this backing
        glDeleteTextures(1, &entry->texture);
        g_directEfbBytes -= entry->bytes;
        *entry = {};
    }
}
void DestroyDirectEfb(uintptr_t destination) {
    DestroyDirectEfbInternal(destination, false);
}
void DestroyDirectEfbCache() {
    for (auto& entry : g_directEfb) DestroyDirectEfb(entry.destination);
}

bool CopyDirectEfb(const EfbFrameCommand& command, float scaleX, float scaleY,
                   float sourceLeft = 0.0f, float sourceTop = 0.0f,
                   bool gpuAlreadySynchronized = false) {
    // Depth copies require a separate depth conversion implementation.
    switch (command.format) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6:
    case 0x20: case 0x22: case 0x23: case 0x27: case 0x28:
    case 0x29: case 0x2a: case 0x2b: case 0x2c: break;
    default: return false;
    }
    const int x = std::clamp(int(std::lround((command.srcLeft - sourceLeft) * scaleX)), 0, DISPLAY_WIDTH);
    const int y = std::clamp(int(std::lround((command.srcTop - sourceTop) * scaleY)), 0, DISPLAY_HEIGHT);
    const int x1 = std::clamp(int(std::lround((command.srcLeft + command.srcWidth - sourceLeft) * scaleX)), x, DISPLAY_WIDTH);
    const int y1 = std::clamp(int(std::lround((command.srcTop + command.srcHeight - sourceTop) * scaleY)), y, DISPLAY_HEIGHT);
    const uint32_t sw = x1 - x, sh = y1 - y;
    if (!sw || !sh || !command.dstWidth || !command.dstHeight || has_cached_mem) return false;
    const auto& surface = gxm_color_surfaces[gxm_back_buffer_index];
    if (sceGxmColorSurfaceGetFormat(&surface) != SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR ||
        sceGxmColorSurfaceGetType(&surface) != SCE_GXM_COLOR_SURFACE_LINEAR ||
        sceGxmColorSurfaceGetScaleMode(&surface) != SCE_GXM_COLOR_SURFACE_SCALE_NONE) return false;
    const auto* base = static_cast<const uint8_t*>(sceGxmColorSurfaceGetData(&surface));
    const size_t sourcePitch = size_t(sceGxmColorSurfaceGetStrideInPixels(&surface)) * 4;
    if (!base || sourcePitch < size_t(x1) * 4) return false;
    auto* entry = FindDirectEfb(command.destination);
    const size_t oldBytes = entry ? entry->bytes : 0;
    uint32_t dw = std::min<uint32_t>(command.dstWidth, sw);
    uint32_t dh = std::min<uint32_t>(command.dstHeight, sh);
#if MKW_VITA_EFB_NATIVE_RES_COPY
    const size_t nativeBytes = size_t((sw + 7u) & ~7u) * sh * 4;
    if (nativeBytes <= kDirectEfbBudget - (g_directEfbBytes - oldBytes)) { dw = sw; dh = sh; }
#endif
    const size_t bytes = size_t((dw + 7u) & ~7u) * dh * 4;
    if (bytes > kDirectEfbBudget - (g_directEfbBytes - oldBytes)) return false;
    if (!gpuAlreadySynchronized) glFinish(); // finish source scene and prior destination readers
    if (entry && (entry->width != dw || entry->height != dh)) {
        DestroyDirectEfbInternal(command.destination, true);
        entry = nullptr;
    }
    if (!entry) {
        for (auto& candidate : g_directEfb) if (!candidate.destination) { entry = &candidate; break; }
        if (!entry || !g_textureScratch || size_t(dw) * dh * 4 > kTextureScratchBytes) return false;
        std::memset(g_textureScratch.get(), 0, size_t(dw) * dh * 4);
        GLuint texture = 0;
        glGenTextures(1, &texture);
        if (!texture) return false;
        glBindTexture(GL_TEXTURE_2D, texture);
        while (glGetError() != GL_NO_ERROR) {}
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dw, dh, 0, GL_RGBA, GL_UNSIGNED_BYTE, g_textureScratch.get());
        if (glGetError() != GL_NO_ERROR || !vglGetTexDataPointer(GL_TEXTURE_2D)) {
            glDeleteTextures(1, &texture);
            return false;
        }
        *entry = {command.destination, texture, dw, dh, bytes};
        g_directEfbBytes += bytes;
    }
    glBindTexture(GL_TEXTURE_2D, entry->texture);
    auto* backing = vglGetGxmTexture(GL_TEXTURE_2D);
    auto* dst = static_cast<uint8_t*>(vglGetTexDataPointer(GL_TEXTURE_2D));
    if (!backing || !dst || sceGxmTextureGetType(backing) != SCE_GXM_TEXTURE_LINEAR ||
        sceGxmTextureGetFormat(backing) != SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR) return false;
    const auto* src = base + size_t(y) * sourcePitch + size_t(x) * 4;
    const size_t pitch = size_t((dw + 7u) & ~7u) * 4;
    bool copied = false;
    if (sw == dw && sh == dh) {
        copied = sceGxmTransferCopy(sw, sh, 0, 0, SCE_GXM_TRANSFER_COLORKEY_NONE,
            SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TRANSFER_LINEAR, src, 0, 0, sourcePitch,
            SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TRANSFER_LINEAR, dst, 0, 0, pitch,
            nullptr, 0, nullptr) >= 0;
        if (copied) copied = sceGxmTransferFinish() >= 0;
    }
    if (!copied) {
        for (uint32_t row = 0; row < dh; ++row) {
            const auto* source = src + size_t(row * sh / dh) * sourcePitch;
            auto* target = dst + row * pitch;
            if (sw == dw) std::memcpy(target, source, size_t(dw) * 4);
            else for (uint32_t col = 0; col < dw; ++col)
                std::memcpy(target + col * 4, source + (col * sw / dw) * 4, 4);
        }
        __sync_synchronize();
    }
    // Apply GX copy-channel selection to the resident RGBA backing. Color
    // passthrough keeps the transfer-only path; other formats need this pass.
    if (command.format != 5 && command.format != 6) {
        const auto q4 = [](uint8_t value) { return uint8_t((value >> 4) * 17); };
        for (uint32_t row = 0; row < dh; ++row) {
            for (uint32_t col = 0; col < dw; ++col) {
                auto* p = dst + row * pitch + col * 4;
                const uint8_t r = p[0], g = p[1], b = p[2], a = p[3];
                uint8_t intensity = r, alpha = a;
                switch (command.format) {
                case 0: case 1: case 2: case 3:
                    intensity = uint8_t(std::clamp(int(std::lround(
                        .257f * r + .504f * g + .098f * b + 16.f)), 0, 255));
                    if (command.format == 0 || command.format == 2) intensity = q4(intensity);
                    alpha = command.format < 2 ? intensity : (command.format == 2 ? q4(a) : a);
                    break;
                case 4: p[3] = 255; continue;
                case 0x20: intensity = q4(r); alpha = intensity; break;
                case 0x22: intensity = q4(r); alpha = q4(a); break;
                case 0x23: break;
                case 0x27: intensity = alpha = a; break;
                case 0x28: alpha = r; break;
                case 0x29: intensity = alpha = g; break;
                case 0x2a: intensity = alpha = b; break;
                case 0x2b: alpha = g; break;
                case 0x2c: intensity = g; alpha = b; break;
                }
                p[0] = p[1] = p[2] = intensity;
                p[3] = alpha;
            }
        }
        __sync_synchronize();
    }
    return true;
}
