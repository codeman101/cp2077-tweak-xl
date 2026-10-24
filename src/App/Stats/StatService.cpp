#include "StatService.hpp"
#include "App/Tweaks/TweakService.hpp"
#include "Core/Facades/Container.hpp"
#if defined(__APPLE__)
#include "MacDiag.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#endif

namespace
{
constexpr auto BaseStatPrefix = "BaseStats.";
constexpr auto BaseStatPrefixLength = std::char_traits<char>::length(BaseStatPrefix);
constexpr auto BaseStatCount = static_cast<uint32_t>(Red::game::data::StatType::Count);
constexpr auto InvalidStat = static_cast<uint32_t>(Red::game::data::StatType::Invalid);

bool s_statTypesModified = false;
#if defined(__APPLE__)
bool s_statsHookInstalled = false;
#endif

#if defined(__APPLE__)
// R23 (macOS). Counters of queries for custom stat types (index above the Invalid entry), printed only with TWEAKXL_MAC_DIAG=1.
std::atomic<uint32_t> s_customRangeQueries{0};
std::atomic<uint32_t> s_customFlagQueries{0};

// R23 self-test (TWEAKXL_MAC_DIAG=1 only), read only. InitializeParams runs right after the hook that registers a custom stat, so the params
// array holds the new stat only a moment later. A short-lived thread waits for that and then asks the game's own getters (the hooked
// functions, exactly the call the game makes) about the custom stat, a base stat, the Count and Invalid entries and an out-of-range index.
void SelfTestGetters(void* aSystem, uint32_t aStat, const std::string& aName)
{
    std::thread([aSystem, aStat, aName]() {
        auto& statParams = Raw::StatsDataSystem::StatParams::Ref(aSystem);
        for (int i = 0; i < 400 && statParams.Size() <= aStat; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

        if (statParams.Size() <= aStat)
        {
            MAC_DIAG("[selftest] stat getters %s FAIL: params array never reached stat %u (size %u)\n", aName.c_str(), aStat,
                     statParams.Size());
            return;
        }

        // The Raw functions resolve to the original once a hook is attached, so the game's own call path is used here: the three getters
        // are virtuals (Mac vtable slots 0x208, 0x210, 0x218), called through the object's vtable exactly as the game does.
        auto* vtable = *reinterpret_cast<void***>(aSystem);
        using RangeFn = Red::StatRange (*)(void*, uint32_t);
        using FlagsFn = uint32_t (*)(void*, uint32_t);
        using CheckFn = bool (*)(void*, uint32_t, uint32_t);
        const auto getRange = reinterpret_cast<RangeFn>(vtable[0x208 / sizeof(void*)]);
        const auto getFlags = reinterpret_cast<FlagsFn>(vtable[0x210 / sizeof(void*)]);
        const auto checkFlag = reinterpret_cast<CheckFn>(vtable[0x218 / sizeof(void*)]);

        const auto range = getRange(aSystem, aStat);
        const auto flags = getFlags(aSystem, aStat);
        const bool hasAll = checkFlag(aSystem, aStat, flags);
        const bool hasOther = checkFlag(aSystem, aStat, ~flags);
        const auto expected = statParams[aStat];
        const bool pass = range.min == expected.min && range.max == expected.max && flags == expected.flags
                          && (flags == 0 ? !hasAll : hasAll) && !hasOther;
        MAC_DIAG("[selftest] stat getters %s id=%u range=(%g,%g) flags=%u checkOwn=%d checkOther=%d expected=(%g,%g,%u) %s\n",
                 aName.c_str(), aStat, range.min, range.max, flags, hasAll ? 1 : 0, hasOther ? 1 : 0, expected.min, expected.max,
                 expected.flags, pass ? "PASS" : "FAIL");

        const auto base = getRange(aSystem, 1);
        const auto dummy = getRange(aSystem, BaseStatCount);
        const auto invalid = getRange(aSystem, InvalidStat);
        const auto outside = getRange(aSystem, statParams.Size() + 100);
        const bool boundsPass = base.min == statParams[1].min && base.max == statParams[1].max && invalid.min == 0.0f
                                && invalid.max == 0.0f && outside.min == 0.0f && outside.max == 0.0f;
        MAC_DIAG("[selftest] stat getters bounds base(1)=(%g,%g) count=(%g,%g) invalid=(%g,%g) outside=(%g,%g) %s\n", base.min, base.max,
                 dummy.min, dummy.max, invalid.min, invalid.max, outside.min, outside.max, boundsPass ? "PASS" : "FAIL");
        MAC_DIAG("[selftest] stat getters handler calls: custom range=%u custom flags=%u\n", s_customRangeQueries.load(),
                 s_customFlagQueries.load());
    }).detach();
}
#endif
}

#if defined(__APPLE__)
void App::StatService::OnBootstrap()
{
    // Prevent double-initialization (game calls Main twice)
    if (s_statsHookInstalled)
    {
        return;
    }
    
    // Stats hooks are optional on macOS - addresses may not be available
    // TweakXL core functionality works without stats modification support
    auto result = HookAfter<Raw::StatsDataSystem::InitializeRecords>(&OnInitializeStats);
    if (!result)
    {
#if defined(__APPLE__)
        LogWarning("StatService: Stats hooks unavailable (the hook was not installed: unknown address, or the offset is not on "
                   "the runtime hook allowlist). Custom stat types will not be supported.");
#else
        LogWarning("StatService: Stats hooks unavailable (address not found). "
                   "Custom stat types will not be supported.");
#endif
    }
    else
    {
        s_statsHookInstalled = true;
        LogInfo("StatService: Stats hooks installed successfully. Custom stat types enabled.");
    }
}

void App::StatService::OnShutdown()
{
    // Detach all stats hooks (best-effort).
    Unhook<Raw::StatsDataSystem::InitializeRecords>();

    if (s_statTypesModified)
    {
        Unhook<Raw::StatsDataSystem::GetStatRange>();
        Unhook<Raw::StatsDataSystem::GetStatFlags>();
        Unhook<Raw::StatsDataSystem::CheckStatFlag>();
    }

    s_statTypesModified = false;
    s_statsHookInstalled = false;
}
#else
void App::StatService::OnBootstrap()
{
    HookAfter<Raw::StatsDataSystem::InitializeRecords>(&OnInitializeStats).OrThrow();
}
#endif

void App::StatService::OnInitializeStats(void* aSystem)
{
#if defined(__APPLE__)
    // Runs on a game job thread right after the original InitializeRecords filled the records array (StatType::Count entries).
    MAC_DIAG("[stat] InitializeRecords finished, records.size=%u\n", Raw::StatsDataSystem::StatRecords::Ref(aSystem).Size());
#endif
    RegisterStats(aSystem, Core::Resolve<TweakService>()->GetChangelog().GetAffectedRecords());
    RegisterStats(aSystem, Core::Resolve<TweakService>()->GetManager().GetEnums());
}

void App::StatService::RegisterStats(void* aStatSystem, const Core::Set<Red::TweakDBID>& aRecordIDs)
{
    auto statRecords = Raw::StatsDataSystem::StatRecords::Ptr(aStatSystem);
    auto statTypeEnum = Red::GetDescriptor<Red::game::data::StatType>();
#if defined(__APPLE__)
    if (!statTypeEnum)
    {
        // R23. The enum lookup by name failed (it did on the first Mac boot, before the clang enum-name fix); never dereference it.
        LogError("StatService: the gamedataStatType enum was not found. Custom stat types will not be registered.");
        return;
    }
#endif

    auto& tweakManager = Core::Resolve<TweakService>()->GetManager();

    for (const auto& recordID : aRecordIDs)
    {
        const auto& recordName = tweakManager.GetName(recordID);

        if (!recordName.starts_with(BaseStatPrefix))
            continue;

        auto enumNameProp = tweakManager.GetFlat({recordID, ".enumName"});

        if (!enumNameProp)
            continue;

        auto enumName = enumNameProp.As<Red::CString>().c_str();

        if (statTypeEnum->HasOption(enumName))
            continue;

        if (recordName.substr(BaseStatPrefixLength) != enumName)
        {
            LogError("{}: Enum name must match the record name.", recordName);
            continue;
        }

        if (statRecords->Size() == BaseStatCount)
        {
            // Add dummy entries for "Count" and "Invalid"
            statRecords->EmplaceBack();
            statRecords->EmplaceBack();
        }

        const auto enumValue = statRecords->Size();

        statTypeEnum->AddOption(enumValue, enumName);
        statRecords->PushBack(recordID);

        if (!s_statTypesModified)
        {
            s_statTypesModified = true;
#if defined(__APPLE__)
            // This runs on a game job thread, so a failure must not throw. The three getters are virtuals the game calls from many
            // threads; the hooks are installed once, before the first custom stat becomes visible through the records array.
            const bool hooked = static_cast<bool>(Hook<Raw::StatsDataSystem::GetStatRange>(&OnGetStatRange))
                                && static_cast<bool>(Hook<Raw::StatsDataSystem::GetStatFlags>(&OnGetStatFlags))
                                && static_cast<bool>(Hook<Raw::StatsDataSystem::CheckStatFlag>(&OnCheckStatFlag));
            if (hooked)
            {
                LogInfo("StatService: stat getter hooks installed (custom stat types are readable).");
            }
            else
            {
                LogError("StatService: could not install the stat getter hooks (offset not on the runtime hook allowlist?). "
                         "Custom stat types will read as unknown.");
            }
#else
            Hook<Raw::StatsDataSystem::GetStatRange>(&OnGetStatRange).OrThrow();
            Hook<Raw::StatsDataSystem::GetStatFlags>(&OnGetStatFlags).OrThrow();
            Hook<Raw::StatsDataSystem::CheckStatFlag>(&OnCheckStatFlag).OrThrow();
#endif
        }

        {
            const auto record = tweakManager.GetRecord(recordID);
            Raw::StatRecord::EnumValue::Ref(record) = enumValue;
#if defined(__APPLE__)
            LogInfo("StatService: custom stat type {} applied (enum value {}).", enumName, enumValue);
            MAC_DIAG("[stat] custom stat %s: records.size=%u enumValue=%u readback=%u hasOption=%d\n", enumName, statRecords->Size(),
                     enumValue, static_cast<uint32_t>(Raw::StatRecord::EnumValue::Ref(record)),
                     statTypeEnum->HasOption(enumName) ? 1 : 0);
            if (Mac::DiagEnabled() && s_statTypesModified)
            {
                SelfTestGetters(aStatSystem, enumValue, enumName);
            }
#endif
        }
    }
}

#if defined(__APPLE__)
Red::StatRange App::StatService::OnGetStatRange(void* aSystem, uint32_t aStat)
{
    // The Mac function takes (this, stat) and returns {min, max} in s0:s1. Out of range gives {0, 0}, like the original.
    if (aStat != InvalidStat)
    {
        auto& statParams = Raw::StatsDataSystem::StatParams::Ref(aSystem);
        auto& statLock = Raw::StatsDataSystem::StatLock::Ref(aSystem);

        std::shared_lock _(statLock);
        if (aStat < statParams.Size())
        {
            const auto& params = statParams[aStat];
            if (aStat > InvalidStat && Mac::DiagEnabled() && s_customRangeQueries.fetch_add(1) == 0)
            {
                MAC_DIAG("[stat] first GetStatRange query of custom stat %u: min=%f max=%f\n", aStat, params.min, params.max);
            }
            return {params.min, params.max};
        }
    }

    return {0.0f, 0.0f};
}
#else
uint64_t* App::StatService::OnGetStatRange(void* aSystem, uint64_t* aRange, uint32_t aStat)
{
    if (aStat != InvalidStat)
    {
        auto& statParams = Raw::StatsDataSystem::StatParams::Ref(aSystem);
        auto& statLock = Raw::StatsDataSystem::StatLock::Ref(aSystem);

        std::shared_lock _(statLock);
        if (aStat < statParams.Size())
        {
            *aRange = statParams[aStat].range;
            return aRange;
        }
    }

    *aRange = 0;
    return aRange;
}
#endif

uint32_t App::StatService::OnGetStatFlags(void* aSystem, uint32_t aStat)
{
    if (aStat != InvalidStat)
    {
        auto& statParams = Raw::StatsDataSystem::StatParams::Ref(aSystem);
        auto& statLock = Raw::StatsDataSystem::StatLock::Ref(aSystem);

        std::shared_lock _(statLock);
        if (aStat < statParams.Size())
        {
#if defined(__APPLE__)
            if (aStat > InvalidStat && Mac::DiagEnabled() && s_customFlagQueries.fetch_add(1) == 0)
            {
                MAC_DIAG("[stat] first GetStatFlags query of custom stat %u: flags=%u\n", aStat, statParams[aStat].flags);
            }
#endif
            return statParams[aStat].flags;
        }
    }

    return 0;
}

bool App::StatService::OnCheckStatFlag(void* aSystem, uint32_t aStat, uint32_t aFlag)
{
    if (aStat != InvalidStat)
    {
        auto& statParams = Raw::StatsDataSystem::StatParams::Ref(aSystem);
        auto& statLock = Raw::StatsDataSystem::StatLock::Ref(aSystem);

        std::shared_lock _(statLock);
        if (aStat < statParams.Size())
        {
            return statParams[aStat].flags & aFlag;
        }
    }

    return false;
}
