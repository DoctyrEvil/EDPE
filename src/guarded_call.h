// Runs a call that may throw and turns a throw into an error value.
//
// The frame generation provider reports some failures, for example after a GPU reset, by throwing a C++ exception
// instead of returning a code. An exception that escapes into the game aborts it, so a throw is recorded and
// reported as an error the caller already handles. Table-based x64 exception handling costs nothing while nothing
// throws. Kept free of the FidelityFX headers so it can be tested without a GPU or the SDK.
#pragma once

#include <atomic>
#include <exception>

namespace edpe {

// Returns call()'s result. If call throws, sets *threw (when not null), runs onThrow(what) (for logging) and returns
// errorCode. what is the exception's what() text, or nullptr when the thrown object is not a std::exception. The flag
// is only ever set here; clearing it is the caller's decision.
template <typename Code, typename Call, typename OnThrow>
Code callGuarded(Code errorCode, std::atomic<bool>* threw, Call&& call, OnThrow&& onThrow) {
    try {
        return call();
    } catch (const std::exception& e) {
        if (threw) threw->store(true);
        onThrow(e.what());
        return errorCode;
    } catch (...) {
        if (threw) threw->store(true);
        onThrow(static_cast<const char*>(nullptr));
        return errorCode;
    }
}

} // namespace edpe
