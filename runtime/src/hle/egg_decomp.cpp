#include "hle_stubs.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include "memory.h"
#include "recomp_mod_loader.h"
#include "runtime_log.h"

#if defined(MKW_TARGET_VITA)
#include <psp2/kernel/processmgr.h>
#endif

#ifndef MKW_VITA_YAZ0_FAST_DIRECT
#define MKW_VITA_YAZ0_FAST_DIRECT 0
#endif

// Native because a crafted Yaz0 run writes past the caller's buffer
// (github.com/vabold/szsHaxx)
// https://github.com/vabold/Kinoko/blob/main/source/egg/core/Decomp.cc

extern "C" uint32_t EGG_Decomp_decodeSZS_80218c2c(uint32_t src, uint32_t dst)
{
#if defined(MKW_TARGET_VITA) && defined(MKW_VITA_GUEST_IO_PROFILE) && MKW_VITA_GUEST_IO_PROFILE
    const uint64_t profileBeginUs = sceKernelGetProcessTimeWide();
#endif
    const uint32_t expandSize = (static_cast<uint32_t>(MemoryInline::FlatRead8(src + 4)) << 24) |
                                (static_cast<uint32_t>(MemoryInline::FlatRead8(src + 5)) << 16) |
                                (static_cast<uint32_t>(MemoryInline::FlatRead8(src + 6)) << 8) |
                                static_cast<uint32_t>(MemoryInline::FlatRead8(src + 7));

    // A valid Yaz0 stream can never consume more than one flag byte for every
    // eight output literals plus the literals themselves and the 16-byte
    // header. That theoretical bound can be larger than the bytes remaining in
    // the MEM1/MEM2 alias containing `src` even when the *actual* compressed
    // stream is small and fully resident. Mapping the theoretical size then
    // spuriously fails near the end of MEM2 and drops multi-megabyte archives
    // back to per-byte guest reads. Clamp the host mapping to the contiguous
    // RAM extent containing `src`; the decode loop still enforces the smaller
    // of that extent and the Yaz0 worst-case bound before every input read.
    const uint64_t maxCompressedSize64 = 16ull + static_cast<uint64_t>(expandSize) +
                                         ((static_cast<uint64_t>(expandSize) + 7ull) >> 3);
    const uint32_t maxCompressedSize = maxCompressedSize64 <= UINT32_MAX
                                           ? static_cast<uint32_t>(maxCompressedSize64)
                                           : 0u;
    const auto contiguousRamBytesFrom = [](uint32_t address) -> uint32_t {
        const auto remaining = [address](uint32_t begin, uint32_t end) -> uint32_t {
            return address >= begin && address < end ? end - address : 0u;
        };

        if (const uint32_t bytes = remaining(
                Memory::kMem1PhysicalBase,
                Memory::kMem1PhysicalBase + static_cast<uint32_t>(Memory::kMem1Size));
            bytes != 0) {
            return bytes;
        }
        if (const uint32_t bytes = remaining(
                Memory::kMem1CachedBase,
                Memory::kMem1CachedBase + static_cast<uint32_t>(Memory::kMem1Size));
            bytes != 0) {
            return bytes;
        }
        if (const uint32_t bytes = remaining(
                Memory::kMem1UncachedBase,
                Memory::kMem1UncachedBase + static_cast<uint32_t>(Memory::kMem1Size));
            bytes != 0) {
            return bytes;
        }
        if (const uint32_t bytes = remaining(Memory::kMem2PhysicalBase, Memory::kMem2PhysicalEnd);
            bytes != 0) {
            return bytes;
        }
        if (const uint32_t bytes = remaining(Memory::kMem2CachedBase, Memory::kMem2CachedEnd);
            bytes != 0) {
            return bytes;
        }
        return remaining(Memory::kMem2UncachedBase, Memory::kMem2UncachedEnd);
    };

    const uint32_t contiguousInputBytes = contiguousRamBytesFrom(src);
    const uint32_t safeInputBytes =
        maxCompressedSize == 0 || contiguousInputBytes == 0
            ? 0u
            : (maxCompressedSize < contiguousInputBytes ? maxCompressedSize : contiguousInputBytes);
    const uint8_t* directInput = nullptr;
    if (safeInputBytes != 0) {
        try {
            directInput = Memory::GetPointer(src, safeInputBytes);
        } catch (...) {
            directInput = nullptr;
        }
    }

    // Archive decompression expands multi-megabyte SZS files into ordinary
    // MEM1/MEM2 buffers. Resolving that destination once avoids running the
    // guest page-table and executable-write checks for every output byte.
    // Preserve the old scalar path for MMIO, executable ranges, wrapped ranges
    // and unusual mappings so the optimization does not weaken write safety.
    uint8_t* directOutput = nullptr;
    const uint64_t outputEnd = static_cast<uint64_t>(dst) + expandSize;
    const bool wrapsGuest = outputEnd > (uint64_t{1} << 32);
    const bool touchesMmio = static_cast<uint64_t>(dst) < 0xCE000000ull &&
                             outputEnd > 0xCC000000ull;
    if (expandSize != 0 && !wrapsGuest && !touchesMmio &&
        !RecompMod::ExecutableWriteGuardMayHit(dst, expandSize)) {
        try {
            directOutput = Memory::GetPointer(dst, expandSize);
        } catch (...) {
            directOutput = nullptr;
        }
    }

#if MKW_VITA_YAZ0_FAST_DIRECT
    if (directInput != nullptr && directOutput != nullptr) [[likely]] {
        const uint8_t* in = directInput + 16u;
        const uint8_t* const inEnd = directInput + safeInputBytes;
        uint8_t* out = directOutput;
        uint8_t* const outEnd = directOutput + expandSize;
        uint32_t mask = 0;
        uint32_t flags = 0;
        uint32_t literalRunBytes = 0;
        uint32_t bulkNonOverlap = 0;
        uint32_t bulkOverlap = 0;

        const auto malformed = [&](const char* reason) {
            RT_LOG(RT_TAG_HLE) << "decodeSZS: malformed stream from 0x" << std::hex << src
                               << std::dec << ", " << reason << std::endl;
            ShowRuntimeFatalPopup("corrupt compressed file",
                                  "The game stopped decoding a malformed Yaz0 file.");
            std::abort();
        };

        while (out < outEnd) {
            if (mask == 0) {
                if (in >= inEnd) [[unlikely]] malformed("compressed input exceeded safe bound");
                flags = *in++;
                mask = 0x80u;
            }

            if ((flags & mask) != 0) {
                // Consume a whole run of adjacent literal flag bits at once.
                uint32_t run = 0;
                uint32_t probe = mask;
                const size_t outputRemaining = static_cast<size_t>(outEnd - out);
                while (probe != 0 && (flags & probe) != 0 && run < outputRemaining) {
                    ++run;
                    probe >>= 1;
                }
                if (static_cast<size_t>(inEnd - in) < run) [[unlikely]] {
                    malformed("compressed literal run exceeded safe bound");
                }
                std::memcpy(out, in, run);
                in += run;
                out += run;
                literalRunBytes += run;
                mask = probe;
                continue;
            }

            if (inEnd - in < 2) [[unlikely]] malformed("truncated back-reference");
            const uint32_t high = in[0];
            const uint32_t low = in[1];
            in += 2;
            const uint32_t rep = (high << 8) | low;
            const uint32_t distance = (rep & 0x0FFFu) + 1u;
            const size_t producedBytes = static_cast<size_t>(out - directOutput);
            if (distance > producedBytes) [[unlikely]] malformed("back-reference before output");

            uint32_t count = rep >> 12;
            if (count != 0) {
                count += 2u;
            } else {
                if (in >= inEnd) [[unlikely]] malformed("truncated extended back-reference");
                count = static_cast<uint32_t>(*in++) + 18u;
            }
            if (static_cast<size_t>(outEnd - out) < count) [[unlikely]] {
                malformed("output overran declared size");
            }

            const uint8_t* source = out - distance;
            if (distance >= count) {
                std::memcpy(out, source, count);
                ++bulkNonOverlap;
            } else if (distance == 1u) {
                // RLE is common in UI/course archives; libc memset is much cheaper
                // than repeatedly growing a one-byte seed through memcpy calls.
                std::memset(out, source[0], count);
                ++bulkOverlap;
            } else {
                std::memcpy(out, source, distance);
                uint32_t copied = distance;
                while (copied < count) {
                    const uint32_t remaining = count - copied;
                    const uint32_t chunk = std::min(copied, remaining);
                    std::memcpy(out + copied, out, chunk);
                    copied += chunk;
                }
                ++bulkOverlap;
            }
            out += count;
            mask >>= 1;
        }

#if defined(MKW_TARGET_VITA) && defined(MKW_VITA_GUEST_IO_PROFILE) && MKW_VITA_GUEST_IO_PROFILE
        RT_LOGF(RT_TAG_HLE,
                "decodeSZS_profile src=0x%08X dst=0x%08X expand=%u consumed=%u elapsed_us=%llu direct_src=1 direct_dst=1 literal_run_bytes=%u bulk_nonoverlap=%u bulk_overlap=%u fast_direct=1\n",
                src, dst, expandSize, static_cast<unsigned>(in - directInput),
                static_cast<unsigned long long>(sceKernelGetProcessTimeWide() - profileBeginUs),
                literalRunBytes, bulkNonOverlap, bulkOverlap);
#endif
        return expandSize;
    }
#endif

    const auto readOutput = [dst, directOutput](uint32_t index) -> uint8_t {
        if (directOutput != nullptr) [[likely]] {
            return directOutput[index];
        }
        return MemoryInline::FlatRead8(dst + index);
    };
    const auto writeOutput = [dst, directOutput](uint32_t index, uint8_t value) {
        if (directOutput != nullptr) [[likely]] {
            directOutput[index] = value;
            return;
        }
        MemoryInline::FlatWrite8(dst + index, value);
    };
    const auto readInput = [src, directInput, maxCompressedSize, safeInputBytes](uint32_t index) -> uint8_t {
        const uint32_t bound = directInput != nullptr ? safeInputBytes : maxCompressedSize;
        if (bound == 0 || index >= bound) [[unlikely]] {
            RT_LOG(RT_TAG_HLE) << "decodeSZS: malformed stream from 0x" << std::hex << src
                               << std::dec << ", compressed input exceeded safe bound"
                               << std::endl;
            ShowRuntimeFatalPopup("corrupt compressed file",
                                  "The game stopped decoding a malformed Yaz0 file.");
            std::abort();
        }
        if (directInput != nullptr) [[likely]] {
            return directInput[index];
        }
        return MemoryInline::FlatRead8(src + index);
    };

    uint32_t srcIdx = 16;
    uint32_t dstIdx = 0;
    uint32_t mask = 0;
    uint32_t flags = 0;
    uint32_t literalRunBytes = 0;
    uint32_t bulkNonOverlap = 0;
    uint32_t bulkOverlap = 0;

    const auto abortOutputOverrun = [&]() {
        RT_LOG(RT_TAG_HLE) << "decodeSZS: malformed stream from 0x" << std::hex << src
                           << std::dec << ", output overran " << expandSize << " bytes"
                           << std::endl;
        ShowRuntimeFatalPopup("corrupt compressed file",
                              "The game stopped decoding a malformed Yaz0 file.");
        std::abort();
    };

    while (static_cast<int32_t>(dstIdx) < static_cast<int32_t>(expandSize)) {
        if (mask == 0) {
            flags = readInput(srcIdx++);
            mask = 0x80;
        }

        if ((flags & mask) != 0) {
            if (directInput != nullptr && directOutput != nullptr) [[likely]] {
                uint32_t run = 0;
                uint32_t probe = mask;
                while (probe != 0 && (flags & probe) != 0 && dstIdx + run < expandSize) {
                    ++run;
                    probe >>= 1;
                }
                if (srcIdx > safeInputBytes || run > safeInputBytes - srcIdx) [[unlikely]] {
                    (void)readInput(safeInputBytes);
                }
                std::memcpy(directOutput + dstIdx, directInput + srcIdx, run);
                srcIdx += run;
                dstIdx += run;
                literalRunBytes += run;
                mask = probe;
                continue;
            }
            writeOutput(dstIdx++, readInput(srcIdx++));
        } else {
            const uint32_t high = readInput(srcIdx);
            const uint32_t low = readInput(srcIdx + 1);
            srcIdx += 2;

            const uint32_t rep = (high << 8) | low;
            // Without this check dstIdx - distance underflows and the
            // copy leaks guest memory from before the destination buffer.
            const uint32_t distance = (rep & 0xFFF) + 1;
            if (distance > dstIdx) {
                RT_LOG(RT_TAG_HLE) << "decodeSZS: malformed stream from 0x" << std::hex << src
                                   << std::dec << ", back-reference before output" << std::endl;
                ShowRuntimeFatalPopup("corrupt compressed file",
                                      "The game stopped decoding a malformed Yaz0 file.");
                std::abort();
            }
            const uint32_t copyIdx = dstIdx - distance;
            uint32_t count = rep >> 12;
            count = count != 0
                        ? count + 2
                        : static_cast<uint32_t>(readInput(srcIdx++)) + 18;
            if (count > expandSize - dstIdx) [[unlikely]] {
                abortOutputOverrun();
            }

            if (directOutput != nullptr) [[likely]] {
                if (distance >= count) {
                    std::memcpy(directOutput + dstIdx, directOutput + copyIdx, count);
                    ++bulkNonOverlap;
                } else {
                    const uint32_t seed = distance;
                    std::memcpy(directOutput + dstIdx, directOutput + copyIdx, seed);
                    uint32_t produced = seed;
                    while (produced < count) {
                        const uint32_t remaining = count - produced;
                        const uint32_t chunk = produced < remaining ? produced : remaining;
                        std::memcpy(directOutput + dstIdx + produced, directOutput + dstIdx, chunk);
                        produced += chunk;
                    }
                    ++bulkOverlap;
                }
                dstIdx += count;
            } else {
                uint32_t scalarCopyIdx = copyIdx;
                for (uint32_t i = 0; i < count; ++i) {
                    writeOutput(dstIdx++, readOutput(scalarCopyIdx++));
                }
            }
        }

        mask >>= 1;
    }

#if defined(MKW_TARGET_VITA) && defined(MKW_VITA_GUEST_IO_PROFILE) && MKW_VITA_GUEST_IO_PROFILE
    RT_LOGF(RT_TAG_HLE,
            "decodeSZS_profile src=0x%08X dst=0x%08X expand=%u consumed=%u elapsed_us=%llu direct_src=%d direct_dst=%d literal_run_bytes=%u bulk_nonoverlap=%u bulk_overlap=%u\n",
            src, dst, expandSize, srcIdx,
            static_cast<unsigned long long>(sceKernelGetProcessTimeWide() - profileBeginUs),
            directInput != nullptr ? 1 : 0, directOutput != nullptr ? 1 : 0,
            literalRunBytes, bulkNonOverlap, bulkOverlap);
#endif

    return expandSize;
}

PPC_NATIVE_OVERRIDE(80218C2C, EGG_Decomp_decodeSZS_80218c2c, uint32_t,
                    (uint32_t src, uint32_t dst), (src, dst));
