#include "native_stack_call.h"
#include <reent.h>

// Newlib's shared stdio backend performs a seek before every append write.
// Both iostreams and printf-style HLE logs reach these functions. Wrap their
// complete operations, including descriptor lookup and errno handling, so a
// guest coroutine never enters file I/O through this backend on its own stack.
// Other native worker threads have no scheduler and call through directly.
extern "C" _off_t __real__lseek_r(struct _reent*, int, _off_t, int);
extern "C" _ssize_t __real__write_r(struct _reent*, int, const void*, size_t);

extern "C" _off_t __wrap__lseek_r(struct _reent* state, int fd, _off_t offset, int whence)
{
    return HostContext::CallOnNativeStack([=] {
        return __real__lseek_r(state, fd, offset, whence);
    });
}

extern "C" _ssize_t __wrap__write_r(struct _reent* state, int fd,
                                   const void* buffer, size_t bytes)
{
    return HostContext::CallOnNativeStack([=] {
        return __real__write_r(state, fd, buffer, bytes);
    });
}
