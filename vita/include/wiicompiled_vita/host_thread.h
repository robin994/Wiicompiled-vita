#pragma once

#if !defined(MKW_TARGET_VITA)
#error "wiicompiled_vita/host_thread.h is only for the PS Vita target"
#endif

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>

#include <pthread.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <utility>

namespace WiiCompiledVita {

enum class HostThreadRole : uint8_t {
    Guest,
    Render,
    Audio,
    Background,
};

inline int AffinityMaskForRole(HostThreadRole role) noexcept {
    switch (role) {
    case HostThreadRole::Guest:
        return SCE_KERNEL_CPU_MASK_USER_0;
    case HostThreadRole::Render:
        return SCE_KERNEL_CPU_MASK_USER_1;
    case HostThreadRole::Audio:
        return SCE_KERNEL_CPU_MASK_USER_2;
    case HostThreadRole::Background:
        // Background work may use whichever helper core is not busy with the
        // render or audio lane. Never let it migrate onto the guest CPU.
        return SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2;
    }
    return SCE_KERNEL_CPU_MASK_USER_ALL;
}

inline bool ConfigureCurrentThread(HostThreadRole role) noexcept {
    const SceUID threadId = sceKernelGetThreadId();
    if (threadId < 0) {
        return false;
    }
    return sceKernelChangeThreadCpuAffinityMask(threadId, AffinityMaskForRole(role)) >= 0;
}

class HostThread {
public:
    HostThread() noexcept = default;
    HostThread(const HostThread&) = delete;
    HostThread& operator=(const HostThread&) = delete;

    ~HostThread() {
        if (joinable()) {
            join();
        }
    }

    template <typename Function>
    bool start(HostThreadRole role, size_t stackSize, Function&& function) {
        if (joinable_) {
            return false;
        }

        auto context = std::unique_ptr<StartContext>(new (std::nothrow) StartContext{
            role,
            std::function<void()>(std::forward<Function>(function)),
        });
        if (!context) {
            return false;
        }

        pthread_attr_t attributes;
        if (pthread_attr_init(&attributes) != 0) {
            return false;
        }

        // VitaSDK pthreads can otherwise receive a stack as small as 16 KiB.
        // Wii HLE workers enter newlib and sizeable C++ call chains, so never
        // rely on that default.
        if (stackSize < kMinimumStackSize) {
            stackSize = kMinimumStackSize;
        }
        const int stackResult = pthread_attr_setstacksize(&attributes, stackSize);
        if (stackResult != 0) {
            pthread_attr_destroy(&attributes);
            return false;
        }

        const int createResult = pthread_create(&thread_, &attributes, &ThreadEntry, context.get());
        pthread_attr_destroy(&attributes);
        if (createResult != 0) {
            return false;
        }

        context.release();
        joinable_ = true;
        return true;
    }

    bool joinable() const noexcept { return joinable_; }

    void join() noexcept {
        if (!joinable_) {
            return;
        }
        pthread_join(thread_, nullptr);
        joinable_ = false;
    }

    static constexpr size_t kMinimumStackSize = 64 * 1024;

private:
    struct StartContext {
        HostThreadRole role;
        std::function<void()> function;
    };

    static void* ThreadEntry(void* rawContext) noexcept {
        std::unique_ptr<StartContext> context(static_cast<StartContext*>(rawContext));
        ConfigureCurrentThread(context->role);
        context->function();
        return nullptr;
    }

    pthread_t thread_{};
    bool joinable_ = false;
};

} // namespace WiiCompiledVita
