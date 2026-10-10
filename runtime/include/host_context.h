#pragma once

#include <cstddef>

// HostContext is the deliberately small boundary between the guest scheduler
// and the host's cooperative-context facility. Vita uses native SceFiber,
// with Run from the scheduler, Switch between guests and ReturnToThread for
// native-stack calls. Windows uses native Fibers;
// Linux and Intel macOS use libco's System V x86-64 backend. macOS AArch64 uses
// the local assembly backend because it must preserve Darwin's platform-reserved
// x18 register, which libco's AArch64 backend does not save. Its handles are
// only valid on the thread that initialized the scheduler.
namespace HostContext {

using Handle = void*;
using Entry = void (*)(void*);

bool InitializeScheduler(Handle* scheduler);
void ShutdownScheduler(Handle scheduler);

Handle Create(std::size_t stackSize, Entry entry, void* argument);
void Destroy(Handle context);
bool IsCurrent(Handle context);
void Switch(Handle target);

// Execute synchronously on the initialized thread's original stack. A coroutine
// is suspended only for this call and then resumes with its CPU/TLS state intact;
// this does not reschedule guest threads. Before initialization this calls directly.
void RunOnNativeStack(Entry entry, void* argument);

} // namespace HostContext
