#include "TweakExecutor.hpp"
#if defined(__APPLE__)
#include "MacDiag.hpp"
#endif
#include "App/Tweaks/Executable/Scriptable/ScriptInterface.hpp"
#include "App/Tweaks/Executable/Scriptable/ScriptManager.hpp"
#include "App/Tweaks/Executable/Scriptable/ScriptableTweak.hpp"

namespace
{
constexpr auto ApplyMethodName = "OnApply";

static const Red::ClassLocator<App::ScriptableTweak> s_scriptableTweakType;
}

App::TweakExecutor::TweakExecutor(Core::SharedPtr<Red::TweakDBManager> aManager)
    : m_manager(std::move(aManager))
    , m_rtti(Red::CRTTISystem::Get())
{
    InitializeRuntime();
}

void App::TweakExecutor::InitializeRuntime()
{
    ScriptManager::SetManager(m_manager);
    ScriptInterface::SetReflection(m_manager->GetReflection());
}

void App::TweakExecutor::ExecuteTweaks()
{
    try
    {
        // R25. GetClasses with a null base returns every class, which would run every class as a tweak when ScriptableTweak is not
        // registered (its scripts failed to load). Without the base there is nothing to run.
        // Fix found in jackmaxwil/cp2077-tweak-xl-macos src/App/Tweaks/Executable/TweakExecutor.cpp, MIT.
        Red::CClass* tweakBase = s_scriptableTweakType;
        if (!tweakBase)
        {
            LogError("ScriptableTweak is not registered, scriptable tweaks are disabled.");
            return;
        }

        Red::DynArray<Red::CClass*> tweakClasses;
        m_rtti->GetClasses(tweakBase, tweakClasses);

#if defined(__APPLE__)
        // R8 diagnostics (TWEAKXL_MAC_DIAG=1 only): the scriptable tweak classes the RTTI system hands out.
        if (Mac::DiagEnabled())
        {
            // R25 self-test: the reason for the null guard above. GetClasses with a null base hands out the whole class table.
            Red::DynArray<Red::CClass*> allClasses;
            m_rtti->GetClasses(nullptr, allClasses);
            MAC_DIAG("[selftest] R25 GetClasses(null base) returns %u classes, GetClasses(ScriptableTweak) returns %u (%s)\n",
                     allClasses.size, tweakClasses.size, allClasses.size > 1000 && allClasses.size > tweakClasses.size ? "PASS" : "FAIL");
            Red::CClass* base = s_scriptableTweakType;
            const auto expected = Red::CName("ScriptableTweak").hash;
            LogInfo("[diag] base ptr={} name={:x} expected={:x} isAbstract={} funcs={} count={}",
                    static_cast<void*>(base),
                    base ? base->name.hash : 0,
                    expected,
                    base ? static_cast<uint32_t>(base->flags.isAbstract) : 0u,
                    base ? base->funcs.size : 0u,
                    tweakClasses.size);
            for (auto* tweakClass : tweakClasses)
            {
                const auto* parent = tweakClass ? tweakClass->parent : nullptr;
                LogInfo("[diag] class ptr={} name={:x} isAbstract={} isNative={} parent={} parentName={:x} funcs={} props={}",
                        static_cast<void*>(tweakClass),
                        tweakClass ? tweakClass->name.hash : 0,
                        tweakClass ? static_cast<uint32_t>(tweakClass->flags.isAbstract) : 0u,
                        tweakClass ? static_cast<uint32_t>(tweakClass->flags.isNative) : 0u,
                        static_cast<const void*>(parent),
                        parent ? parent->name.hash : 0,
                        tweakClass ? tweakClass->funcs.size : 0u,
                        tweakClass ? tweakClass->props.size : 0u);
            }
        }
#endif

        if (tweakClasses.IsEmpty())
            return;

        LogInfo("Executing scriptable tweaks...");

        // ScriptedManager scopedManager(m_manager);

        for (auto* tweakClass : tweakClasses)
            Execute(tweakClass);

        LogInfo("Execution completed.");
    }
    catch (const std::exception& ex)
    {
        LogError(ex.what());
    }
    catch (...)
    {
        LogError("An unknown error occurred while trying to execute tweaks.");
    }
}

void App::TweakExecutor::ExecuteTweak(Red::CName aTweakName)
{
    auto tweakClass = m_rtti->GetClass(aTweakName);

    if (!tweakClass)
    {
        LogError(R"(Tweak class "{}" not found.)", aTweakName.ToString());
        return;
    }

    if (!tweakClass->IsA(s_scriptableTweakType))
    {
        LogError(R"(Tweak class "{}" must inherit from "{}".)",
                 aTweakName.ToString(), s_scriptableTweakType->GetName().ToString());
        return;
    }

    if (tweakClass->flags.isAbstract)
    {
        LogError(R"(Tweak class "{}" is abstract and cannot be executed.)", aTweakName.ToString());
        return;
    }

    // ScriptedManager scopedManager(m_manager);

    if (Execute(tweakClass))
        LogInfo("Execution completed.");
}

bool App::TweakExecutor::Execute(Red::CClass* aTweakClass)
{
    try
    {
        if (aTweakClass->flags.isAbstract)
            return false;

        auto applyCallback = aTweakClass->GetFunction(ApplyMethodName);

        if (!applyCallback || applyCallback->flags.hasUndefinedBody)
        {
            LogError(R"(Tweak class "{}" doesn't have "{}" method.)",
                     aTweakClass->GetName().ToString(), ApplyMethodName);
            return false;
        }

        auto tweakHandle = Red::Handle(reinterpret_cast<ScriptableTweak*>(aTweakClass->CreateInstance()));

        if (!tweakHandle)
        {
            LogError(R"(Tweak instance "{}" cannot be constructed.)",
                     aTweakClass->GetName().ToString());
            return false;
        }

        LogInfo(R"(Executing "{}"...)", aTweakClass->GetName().ToString());

        auto stack = Red::CStack(tweakHandle.instance);

        applyCallback->Execute(&stack);
    }
    catch (const std::exception& ex)
    {
        LogError(ex.what());
        return false;
    }
    catch (...)
    {
        LogError("An unknown error occurred.");
        return false;
    }

    return true;
}
