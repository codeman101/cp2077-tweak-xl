#pragma once

#include <cstdio>
#include <cstdlib>

namespace Red
{
class TypeInfoRegistrar
{
public:
    using Callback = void(*)();

    TypeInfoRegistrar(Callback aRegister, Callback aDescribe)
    {
        AddRegisterCallback(aRegister);
        AddDescribeCallback(aDescribe);
    }

    static inline void RegisterDiscovered()
    {
        QueuePendingRegisterCallbacks();
        QueuePendingDescribeCallbacks();
    }

    // Run the queued RedLib callbacks on this thread. macOS calls this once from
    // tweakxl_registerNatives at InitScripts, because the game does not call them.
    // Does not queue them onto CRTTISystem, so OnBootstrap stays unused.
    static inline void RunPending()
    {
        ProcessPendingRegisterCallbacks();
        ProcessPendingDescriberCallbacks();
    }

    static inline std::size_t PendingRegisterCount()
    {
        return s_registerCallbacks.size();
    }

    static inline std::size_t PendingDescribeCount()
    {
        return s_describeCallbacks.size();
    }

    static inline void AddRegisterCallback(Callback aRegister)
    {
        if (aRegister)
        {
            s_registerCallbacks.push_back(aRegister);
        }
    }

    static inline void AddDescribeCallback(Callback aDescribe)
    {
        if (aDescribe)
        {
            s_describeCallbacks.push_back(aDescribe);
        }
    }

private:
    static inline void QueuePendingRegisterCallbacks()
    {
        if (!s_registerCallbacks.empty())
        {
            CRTTISystem::Get()->AddRegisterCallback(&OnRegister);
        }
    }

    static inline void QueuePendingDescribeCallbacks()
    {
        if (!s_describeCallbacks.empty())
        {
            CRTTISystem::Get()->AddPostRegisterCallback(&OnDescribe);
        }
    }

    static inline void ProcessPendingRegisterCallbacks()
    {
        auto callbacks = std::move(s_registerCallbacks);
        for (const auto& callback :callbacks)
        {
            callback();
        }
    }

    static inline void ProcessPendingDescriberCallbacks()
    {
        auto callbacks = std::move(s_describeCallbacks);
        for (const auto& callback :callbacks)
        {
            callback();
        }
    }

    static inline void OnRegister()
    {
#if defined(__APPLE__)
        // R8 diagnostics (TWEAKXL_MAC_DIAG=1 only).
        if (std::getenv("TWEAKXL_MAC_DIAG") && std::getenv("TWEAKXL_MAC_DIAG")[0] == '1') std::fprintf(stderr, "[RedLib] OnRegister %zu\n", s_registerCallbacks.size());
#endif
        ProcessPendingRegisterCallbacks();
        QueuePendingRegisterCallbacks();
    }

    static inline void OnDescribe()
    {
#if defined(__APPLE__)
        // R8 diagnostics (TWEAKXL_MAC_DIAG=1 only).
        if (std::getenv("TWEAKXL_MAC_DIAG") && std::getenv("TWEAKXL_MAC_DIAG")[0] == '1') std::fprintf(stderr, "[RedLib] OnDescribe %zu\n", s_describeCallbacks.size());
#endif
        ProcessPendingDescriberCallbacks();
        QueuePendingDescribeCallbacks();
    }

    static inline std::vector<Callback> s_registerCallbacks;
    static inline std::vector<Callback> s_describeCallbacks;
};
}
