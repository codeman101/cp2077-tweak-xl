#pragma once

#include "Core/Foundation/Feature.hpp"
#include "Core/Hooking/HookingDriver.hpp"
#include <RED4ext/Api/v1/Sdk.hpp>
#include <iostream>
#include "MacDiag.hpp"

namespace Support
{
class MacOSHookingProvider
    : public Core::Feature
    , public Core::HookingDriver
{
public:
    MacOSHookingProvider(RED4ext::v1::PluginHandle aPlugin, const RED4ext::v1::Sdk* aSdk) noexcept
        : m_plugin(aPlugin)
        , m_sdk(aSdk)
    {
    }

protected:
    void OnInitialize() override
    {
        MAC_DIAG_RUN(std::cerr << "[MacOSHookingProvider] Setting as default hooking driver" << std::endl);
        SetDefault(*this);
    }

    bool HookAttach(uintptr_t aAddress, void* aCallback) override
    {
        MAC_DIAG_RUN(std::cerr << "[MacOSHookingProvider] HookAttach (no original): "
                               << std::hex << aAddress << " -> " << aCallback << std::dec << std::endl);
        
        if (!m_sdk || !m_sdk->hooking)
        {
            std::cerr << "[MacOSHookingProvider] SDK or hooking interface not available" << std::endl;
            return false;
        }
        
        return m_sdk->hooking->Attach(m_plugin, reinterpret_cast<void*>(aAddress), aCallback, nullptr);
    }

    bool HookAttach(uintptr_t aAddress, void* aCallback, void** aOriginal) override
    {
        MAC_DIAG_RUN(std::cerr << "[MacOSHookingProvider] HookAttach (with original): "
                               << std::hex << aAddress << " -> " << aCallback << std::dec << std::endl);

        if (!m_sdk || !m_sdk->hooking)
        {
            std::cerr << "[MacOSHookingProvider] SDK or hooking interface not available" << std::endl;
            return false;
        }

        if (!m_sdk->hooking->Attach(m_plugin, reinterpret_cast<void*>(aAddress), aCallback, aOriginal))
        {
            return false;
        }

        // R23. RED4ext-macos installs a detour only on an offset that is on its GUM_OFFSETS allowlist; for any other offset Attach reports
        // success and hands back the target itself as the original. A hook that is not installed must not be reported as installed.
        if (aOriginal && *aOriginal == reinterpret_cast<void*>(aAddress))
        {
            std::cerr << "[MacOSHookingProvider] hook NOT installed (offset not on the GUM_OFFSETS allowlist): "
                      << std::hex << aAddress << std::dec << std::endl;
            return false;
        }

        MAC_DIAG_RUN(std::cerr << "[MacOSHookingProvider] hook INSTALLED: " << std::hex << aAddress << std::dec << std::endl);
        return true;
    }

    bool HookDetach(uintptr_t aAddress) override
    {
        MAC_DIAG_RUN(std::cerr << "[MacOSHookingProvider] HookDetach: " << std::hex << aAddress << std::dec << std::endl);
        
        if (!m_sdk || !m_sdk->hooking)
        {
            std::cerr << "[MacOSHookingProvider] SDK or hooking interface not available" << std::endl;
            return false;
        }
        
        return m_sdk->hooking->Detach(m_plugin, reinterpret_cast<void*>(aAddress));
    }

private:
    RED4ext::v1::PluginHandle m_plugin;
    const RED4ext::v1::Sdk* m_sdk;
};
}
