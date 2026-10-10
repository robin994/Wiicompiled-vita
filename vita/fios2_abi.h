#pragma once

#include <stddef.h>
#include <stdint.h>

// Minimal documented Vita FIOS2 ABI required by the PSARC backend. VitaSDK
// ships libSceFios2_stub.a but not all user-mode declarations in one public
// header, so keep this small ABI boundary local to the Vita platform layer.
extern "C" {

typedef int32_t SceFiosHandle;
typedef SceFiosHandle SceFiosFH;
typedef int64_t SceFiosSize;
typedef int64_t SceFiosOffset;

struct SceFiosBuffer {
    void* pPtr;
    size_t length;
};

struct SceFiosParams {
    uint32_t header;
    uint32_t profiling;
    uint32_t ioThreadCount;
    uint32_t threadsPerScheduler;
    uint32_t extraFlags;
    uint32_t maxChunk;
    uint8_t maxDecompressorThreadCount;
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t reserved3;
    intptr_t reserved4;
    intptr_t reserved5;
    SceFiosBuffer opStorage;
    SceFiosBuffer fhStorage;
    SceFiosBuffer dhStorage;
    SceFiosBuffer chunkStorage;
    void* pVprintf;
    void* pMemcpy;
    void* pProfileCallback;
    int threadPriority[3];
    int threadAffinity[3];
    int threadStackSize[3];
};

struct SceFiosPsarcDearchiverContext {
    size_t sizeOfContext;
    size_t workBufferSize;
    void* pWorkBuffer;
    intptr_t flags;
    intptr_t reserved[3];
};

int sceFiosInitialize(const SceFiosParams* parameters);
void sceFiosTerminate(void);
int sceFiosIOFilterAdd(int index, void (*callback)(void), void* context);
int sceFiosIOFilterRemove(int index);
void sceFiosIOFilterPsarcDearchiver(void);
SceFiosSize sceFiosArchiveGetMountBufferSizeSync(
    const void* attr, const char* archivePath, const void* openParams);
int sceFiosArchiveMountSync(
    const void* attr, SceFiosFH* outFH, const char* archivePath,
    const char* mountPoint, SceFiosBuffer mountBuffer, const void* openParams);
int sceFiosArchiveUnmountSync(const void* attr, SceFiosFH fh);
int sceFiosFHOpenSync(
    const void* attr, SceFiosFH* outFH, const char* path, const void* openParams);
int sceFiosFHCloseSync(const void* attr, SceFiosFH fh);
SceFiosSize sceFiosFHGetSize(SceFiosFH fh);
SceFiosSize sceFiosFHPreadSync(
    const void* attr, SceFiosFH fh, void* buffer, SceFiosSize length, SceFiosOffset offset);

} // extern "C"

static_assert(sizeof(void*) == 4, "FIOS2 Vita ABI requires 32-bit pointers");
static_assert(sizeof(SceFiosBuffer) == 8, "FIOS2 buffer ABI");
static_assert(sizeof(SceFiosParams) == 116, "FIOS2 params ABI");
static_assert(sizeof(SceFiosPsarcDearchiverContext) == 28, "FIOS2 PSARC context ABI");

constexpr int SCE_FIOS_OK = 0;
constexpr SceFiosFH SCE_FIOS_FH_INVALID = 0;
constexpr unsigned SCE_FIOS_PSARC_DEARCHIVER_WORK_BUFFER_SIZE = 3u * 64u * 1024u;
constexpr unsigned SCE_FIOS_PSARC_DEARCHIVER_WORK_BUFFER_ALIGNMENT = 64u;

constexpr size_t fios_align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
constexpr size_t fios_storage_size(size_t count, size_t item_size) {
    return count * item_size + fios_align_up(fios_align_up(count, 8) / 8, 8);
}
constexpr size_t fios_op_storage_size(size_t count, size_t path_max) {
    return fios_storage_size(count, 168 + path_max);
}
constexpr size_t fios_fh_storage_size(size_t count, size_t path_max) {
    return fios_storage_size(count, 80 + path_max);
}
constexpr size_t fios_dh_storage_size(size_t count, size_t path_max) {
    return fios_storage_size(count, 80 + path_max);
}
constexpr size_t fios_chunk_storage_size(size_t count) {
    return fios_storage_size(count, 64);
}
