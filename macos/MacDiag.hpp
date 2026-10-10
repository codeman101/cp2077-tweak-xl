#pragma once

// R23. Diagnostics and self-tests of the macOS port run only when the environment has TWEAKXL_MAC_DIAG=1 (read once). A default session prints
// only the normal plugin log lines and real errors.
//
//   Mac::DiagEnabled()   true when TWEAKXL_MAC_DIAG is "1".
//   MAC_DIAG(...)        std::fprintf(stderr, ...) that prints only when diagnostics are enabled.
//   MAC_DIAG_RUN(stmt)   runs the statement (a stream expression or a block) only when diagnostics are enabled.

#include <cstdio>
#include <cstdlib>

namespace Mac
{
inline bool DiagEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("TWEAKXL_MAC_DIAG");
        return value && value[0] == '1';
    }();
    return enabled;
}

// The dispatched chunk of a tweak changeset commit (registration of the names of new records) runs on the game's job queue, as on Windows
// (R23, proved in menu boots with the R18 job rows: closure freed by the queue, nothing waits). TWEAKXL_MAC_ASYNC_COMMIT=0 runs it
// synchronously instead.
inline bool AsyncCommitEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("TWEAKXL_MAC_ASYNC_COMMIT");
        return !(value && value[0] == '0');
    }();
    return enabled;
}
}

#define MAC_DIAG(...)                          \
    do                                         \
    {                                          \
        if (::Mac::DiagEnabled())              \
        {                                      \
            std::fprintf(stderr, __VA_ARGS__); \
        }                                      \
    } while (0)

#define MAC_DIAG_RUN(...)         \
    do                            \
    {                             \
        if (::Mac::DiagEnabled()) \
        {                         \
            __VA_ARGS__;          \
        }                         \
    } while (0)
