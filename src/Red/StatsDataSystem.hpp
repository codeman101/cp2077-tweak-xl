#pragma once

#include <type_traits>

namespace Red
{
#if defined(__APPLE__)
// One element of the params array at StatsDataSystem+0xE8: 12 bytes, {min, max, flags} (R23, proved on the Mac by InitializeParams and the
// three getters; same layout as on Windows).
struct StatParams
{
    float min;
    float max;
    uint32_t flags;
};
static_assert(sizeof(StatParams) == 12);

// The Mac GetStatRange returns the two floats in s0:s1 (an AAPCS64 homogeneous float aggregate) with no hidden result pointer, where the
// Windows function writes {min, max} through its second argument. The struct must stay a plain aggregate (trivially copyable, no
// constructors), or the compiler would return it through memory.
struct StatRange
{
    float min;
    float max;
};
static_assert(std::is_trivially_copyable_v<StatRange> && sizeof(StatRange) == 8);
#else
struct StatParams
{
#pragma pack(push, 1)
    union
    {
        uint64_t range;
        struct
        {
            float min;
            float max;
        };
    };
#pragma pack(pop)
    uint32_t flags;
};
#endif
}

namespace Raw::StatsDataSystem
{
using StatRecords = Core::OffsetPtr<0xD8, Red::DynArray<Red::TweakDBID>>;
using StatParams = Core::OffsetPtr<0xE8, Red::DynArray<Red::StatParams>>;
using StatLock = Core::OffsetPtr<0xFC, Red::SharedSpinLock>;

constexpr auto InitializeRecords = Core::RawFunc<
    /* addr = */ Red::AddressLib::StatsDataSystem_InitializeRecords,
    /* type = */ void (*)(void* aSystem)>();

constexpr auto InitializeParams = Core::RawFunc<
    /* addr = */ Red::AddressLib::StatsDataSystem_InitializeParams,
    /* type = */ void (*)(void* aSystem)>();

#if defined(__APPLE__)
constexpr auto GetStatRange = Core::RawFunc<
    /* addr = */ Red::AddressLib::StatsDataSystem_GetStatRange,
    /* type = */ Red::StatRange (*)(void* aSystem, uint32_t aStat)>();
#else
constexpr auto GetStatRange = Core::RawFunc<
    /* addr = */ Red::AddressLib::StatsDataSystem_GetStatRange,
    /* type = */ uint64_t* (*)(void* aSystem, uint64_t*, uint32_t aStat)>();
#endif

constexpr auto GetStatFlags = Core::RawFunc<
    /* addr = */ Red::AddressLib::StatsDataSystem_GetStatFlags,
    /* type = */ uint32_t (*)(void* aSystem, uint32_t aStat)>();

constexpr auto CheckStatFlag = Core::RawFunc<
    /* addr = */ Red::AddressLib::StatsDataSystem_CheckStatFlag,
    /* type = */ bool (*)(void* aSystem, uint32_t aStat, uint32_t aFlag)>();
}

namespace Raw::StatRecord
{
using EnumValue = Core::OffsetPtr<0xD8, uint32_t>;
}
