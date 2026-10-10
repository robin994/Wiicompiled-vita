#pragma once

#include "host_context.h"
#include <type_traits>

namespace HostContext {

// Keep the callable, arguments and result alive until the synchronous native
// invocation completes. RunOnNativeStack propagates failure to the caller.
template <class Callback>
auto CallOnNativeStack(Callback callback) {
    using Result = decltype(callback());
    if constexpr (std::is_void_v<Result>) {
        RunOnNativeStack([](void* context) {
            (*static_cast<Callback*>(context))();
        }, &callback);
    } else {
        static_assert(!std::is_reference_v<Result>);
        struct Call { Callback* callback; Result result{}; } call{&callback};
        RunOnNativeStack([](void* context) {
            auto& call = *static_cast<Call*>(context);
            call.result = (*call.callback)();
        }, &call);
        return call.result;
    }
}

} // namespace HostContext
