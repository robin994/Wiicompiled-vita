#define LIBCO_C
#include "libco.h"

/* VitaSDK's newlib exposes <threads.h> but not its machine/_threads.h backend.
 * libco only needs compiler TLS here, which GCC/Clang provide directly. */
#if defined(__vita__) && defined(LIBCO_MP) && !defined(thread_local)
#define thread_local __thread
#endif
#include "settings.h"

#include <psp2/fiber.h>
#include <psp2/sysmodule.h>

#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct co_vita_thread {
    SceFiber fiber;
    void* context;
    unsigned int context_size;
    void (*entrypoint)(void);
    int is_main;
} co_vita_thread;

static thread_local co_vita_thread co_main_thread;
static thread_local co_vita_thread* co_running;
static int co_fiber_module_initialized;

static int co_vita_init_module(void) {
    if (!co_fiber_module_initialized) {
        /* The module is process-global. Treat an already-loaded module as usable;
         * the first SceFiber API call remains the authoritative check. */
        (void)sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER);
        co_fiber_module_initialized = 1;
    }
    return 1;
}

static void co_vita_entry(SceUInt32 arg_on_initialize, SceUInt32 arg_on_run) {
    (void)arg_on_run;
    co_vita_thread* thread = (co_vita_thread*)(uintptr_t)arg_on_initialize;
    co_running = thread;
    thread->entrypoint();

    /* libco entrypoints are expected to switch away rather than return. The
     * WiiCompiled guest-fiber trampoline follows that contract. Keep a safe
     * fallback for accidental returns so execution never falls off a fiber. */
    co_running = &co_main_thread;
    sceFiberReturnToThread(0, NULL);
    abort();
}

cothread_t co_active(void) {
    if (!co_running) {
        co_vita_init_module();
        co_main_thread.is_main = 1;
        co_running = &co_main_thread;
    }
    return (cothread_t)co_running;
}

cothread_t co_derive(void* memory, unsigned int size, void (*entrypoint)(void)) {
    (void)memory;
    (void)size;
    (void)entrypoint;
    /* SceFiber owns metadata outside the caller-provided stack buffer, so the
     * legacy in-place libco construction contract cannot be represented safely.
     * WiiCompiled only uses co_create(). */
    return (cothread_t)0;
}

cothread_t co_create(unsigned int size, void (*entrypoint)(void)) {
    const unsigned int min_context_size = 32u * 1024u;
    const unsigned int alignment = 4096u;

    if (!entrypoint) return (cothread_t)0;
    co_active();

    if (size < min_context_size) size = min_context_size;
    size = (size + alignment - 1u) & ~(alignment - 1u);

    co_vita_thread* thread = (co_vita_thread*)calloc(1, sizeof(*thread));
    if (!thread) return (cothread_t)0;

    thread->context = memalign(alignment, size);
    if (!thread->context) {
        free(thread);
        return (cothread_t)0;
    }

    thread->context_size = size;
    thread->entrypoint = entrypoint;

    if (_sceFiberInitializeImpl(&thread->fiber,
                                (char*)"WiiCompiled",
                                co_vita_entry,
                                (SceUInt32)(uintptr_t)thread,
                                thread->context,
                                (SceSize)thread->context_size,
                                NULL) < 0) {
        free(thread->context);
        free(thread);
        return (cothread_t)0;
    }

    return (cothread_t)thread;
}

void co_delete(cothread_t handle) {
    co_vita_thread* thread = (co_vita_thread*)handle;
    if (!thread || thread->is_main || thread == co_running) return;

    sceFiberFinalize(&thread->fiber);
    free(thread->context);
    free(thread);
}

void co_switch(cothread_t handle) {
    co_vita_thread* target = (co_vita_thread*)handle;
    co_vita_thread* previous = co_running;

    if (!target || target == previous) return;
    if (!previous) previous = (co_vita_thread*)co_active();

    co_running = target;

    if (target->is_main) {
        sceFiberReturnToThread(0, NULL);
    } else if (previous->is_main) {
        sceFiberRun(&target->fiber, 0, NULL);
    } else {
        sceFiberSwitch(&target->fiber, 0, NULL);
    }
}

int co_serializable(void) {
    return 0;
}

#ifdef __cplusplus
}
#endif
