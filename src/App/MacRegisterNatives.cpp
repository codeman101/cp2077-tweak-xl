#if defined(__APPLE__) // macOS only: registration of the TweakXL natives with the Mac game
// Native registration entry for the macOS script layer.
// Pattern from ysrdevs/nightcity-console-mac runtime/red4ext_hooks.js (MIT):
// the Frida script calls tweakxl_registerNatives once, at InitScripts entry.
// This does not enable RedLibProvider::OnBootstrap.

#include "MacDiag.hpp"
#include "Red/TypeInfo.hpp"

#include "App/Facade.hpp"
#include "App/Tweaks/Executable/Scriptable/ScriptBatch.hpp"
#include "App/Tweaks/Executable/Scriptable/ScriptInterface.hpp"
#include "App/Tweaks/Executable/Scriptable/ScriptManager.hpp"
#include "App/Tweaks/Executable/Scriptable/ScriptableTweak.hpp"

#include <RED4ext/Hashing/FNV1a.hpp>
#include <RED4ext/Memory/Vault.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>

// Local stand-in for CRTTIRegistrator_RTTIAsyncId. The game global is not confirmed.
// R9 starts above the game's real async ids. GetNextId increments before use.
extern "C" uint32_t tweakxl_rtti_async_id = 0x7FFF0000;

// Local stand-in for ISerializable_Counter. The game inlines its own increment.
// Ids start at 2^62 so they stay clear of the game's dense ids. Not a game address.
extern "C" volatile int64_t tweakxl_serializable_counter = 1LL << 62;

#if defined(__APPLE__)
// R9: clang instantiates a template's static member only when it is odr-used.
__attribute__((used)) static const void* const kRedLibRegistrars[] = {
    &Red::ClassDefinition<App::Facade>::s_registrar,
    &Red::ClassDefinition<App::ScriptManager>::s_registrar,
    &Red::ClassDefinition<App::ScriptBatch>::s_registrar,
    &Red::ClassDefinition<App::ScriptableTweak>::s_registrar,
    &Red::ClassExpansion<Red::TweakDBInterface, Red::Scope::For<App::ScriptInterface>()>::s_registrar,
};
#endif

namespace
{
// Exported PoolStorageProxy functions. Static. Block comes back in x0/x1.
// dlsym wants one leading underscore. dyld_info prints two.
constexpr uint64_t kTagRtti = 0x10;
constexpr uint64_t kTagFunction = 0x20;
constexpr uint64_t kTagProperty = 0x30;
// R14. Scripted classes live in PoolScript. A C++ Handle to a scripted instance (a ScriptableTweak or a
// ScriptableService subclass written in redscript) frees it through Allocator<PoolScript>, which looks the pool up
// in this vault. Without the entry the lookup returns null and the free faults at +0x10.
constexpr uint64_t kTagScript = 0x40;

struct GameBlock
{
    void* memory;
    uint64_t size;
};

struct PoolOps
{
    const char* name;
    uint64_t tag;
    uintptr_t expect[5];
    GameBlock (*alloc)(uint64_t);
    GameBlock (*allocAligned)(uint64_t, uint32_t);
    GameBlock (*realloc)(GameBlock&, uint64_t);
    GameBlock (*reallocAligned)(GameBlock&, uint64_t, uint32_t);
    void (*freeFn)(GameBlock&);
};

PoolOps g_pools[] = {
    {"8PoolRTTI", kTagRtti, {0x2177D04, 0x2177E28, 0x2177F68, 0x21780B4, 0x217819C}, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"16PoolRTTIFunction", kTagFunction, {0x217844C, 0x2178570, 0x21786B0, 0x21787FC, 0x21788E4}, nullptr, nullptr, nullptr, nullptr, nullptr},
    {"16PoolRTTIProperty", kTagProperty, {0x2178B94, 0x2178CB8, 0x2178DF8, 0x2178F44, 0x217902C}, nullptr, nullptr, nullptr, nullptr, nullptr},
    // R14. Exports checked with nm: Allocate 0x344DCAC, AllocateAligned 0x344DDD0, Reallocate 0x344DF10,
    // ReallocateAligned 0x344E05C, Free 0x344E144.
    {"10PoolScript", kTagScript, {0x344DCAC, 0x344DDD0, 0x344DF10, 0x344E05C, 0x344E144}, nullptr, nullptr, nullptr, nullptr, nullptr},
};

const char* kPoolSuffixes[5] = {
    "8AllocateEy",
    "15AllocateAlignedEyj",
    "10ReallocateERNS0_5BlockEy",
    "17ReallocateAlignedERNS0_5BlockEyj",
    "4FreeERNS0_5BlockE",
};

bool g_vaultSymbolsOk = false;

RED4ext::Memory::Vault g_vault;
RED4ext::Memory::PoolStorage g_rttiStorage;
RED4ext::Memory::PoolStorage g_functionStorage;
RED4ext::Memory::PoolStorage g_propertyStorage;
RED4ext::Memory::PoolStorage g_scriptStorage; // R14
bool g_vaultReady = false;

uintptr_t GameBase()
{
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        if (header && header->filetype == MH_EXECUTE)
        {
            return reinterpret_cast<uintptr_t>(header);
        }
    }
    return reinterpret_cast<uintptr_t>(_dyld_get_image_header(0));
}

void FillPool(uint32_t aIndex, const char* aName, RED4ext::Memory::PoolStorage& aStorage, uint64_t aTag)
{
    auto& node = g_vault.poolRegistry.nodes[aIndex];
    std::memset(&node, 0, sizeof(node));
    node.handle = RED4ext::FNV1a32(aName);
    node.storage = &aStorage;
    aStorage.allocatorStorage = aTag;
    std::strncpy(node.name, aName, sizeof(node.name) - 1);
}

bool AddressInImage(uint32_t aIndex, uintptr_t aAddr)
{
    const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(aIndex));
    if (!header)
    {
        return false;
    }
    const intptr_t slide = _dyld_get_image_vmaddr_slide(aIndex);
    auto* cmd = reinterpret_cast<const load_command*>(header + 1);
    for (uint32_t i = 0; i < header->ncmds; ++i)
    {
        if (cmd->cmd == LC_SEGMENT_64)
        {
            const auto* seg = reinterpret_cast<const segment_command_64*>(cmd);
            const auto start = static_cast<uintptr_t>(seg->vmaddr) + static_cast<uintptr_t>(slide);
            const auto end = start + seg->vmsize;
            if (aAddr >= start && aAddr < end)
            {
                return true;
            }
        }
        cmd = reinterpret_cast<const load_command*>(reinterpret_cast<const uint8_t*>(cmd) + cmd->cmdsize);
    }
    return false;
}

int ImageIndex(const char* aNamePart, uint32_t aFileType)
{
    const uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto* header = reinterpret_cast<const mach_header_64*>(_dyld_get_image_header(i));
        const char* name = _dyld_get_image_name(i);
        if (!header)
        {
            continue;
        }
        if (aFileType != 0 && header->filetype != aFileType)
        {
            continue;
        }
        if (aNamePart && (!name || !std::strstr(name, aNamePart)))
        {
            continue;
        }
        return static_cast<int>(i);
    }
    return -1;
}

void InitVault()
{
    if (g_vaultReady)
    {
        return;
    }
    std::memset(&g_vault, 0, sizeof(g_vault));
    std::memset(&g_rttiStorage, 0, sizeof(g_rttiStorage));
    std::memset(&g_functionStorage, 0, sizeof(g_functionStorage));
    std::memset(&g_propertyStorage, 0, sizeof(g_propertyStorage));
    std::memset(&g_scriptStorage, 0, sizeof(g_scriptStorage)); // R14
    FillPool(0, "PoolRTTI", g_rttiStorage, kTagRtti);
    FillPool(1, "PoolRTTIFunction", g_functionStorage, kTagFunction);
    FillPool(2, "PoolRTTIProperty", g_propertyStorage, kTagProperty);
    FillPool(3, "PoolScript", g_scriptStorage, kTagScript); // R14

    const uintptr_t base = GameBase();
    g_vaultSymbolsOk = true;
    for (auto& pool : g_pools)
    {
        void* slots[5] = {};
        for (int i = 0; i < 5; ++i)
        {
            char symbol[160];
            std::snprintf(symbol, sizeof(symbol), "_ZN3red6memory16PoolStorageProxyINS_%sEE%s", pool.name, kPoolSuffixes[i]);
            void* found = dlsym(RTLD_MAIN_ONLY, symbol);
            const auto addr = reinterpret_cast<uintptr_t>(found);
            const auto rva = found ? addr - base : 0;
            MAC_DIAG( "[vault] %s %s rva=%llx expected=%llx\n", pool.name, kPoolSuffixes[i],
                         static_cast<unsigned long long>(rva), static_cast<unsigned long long>(pool.expect[i]));
            if (!found || rva != pool.expect[i])
            {
                std::fprintf(stderr, "[vault] MISMATCH %s %s\n", pool.name, kPoolSuffixes[i]);
                g_vaultSymbolsOk = false;
                slots[i] = nullptr;
            }
            else
            {
                slots[i] = found;
            }
        }
        pool.alloc = reinterpret_cast<GameBlock (*)(uint64_t)>(slots[0]);
        pool.allocAligned = reinterpret_cast<GameBlock (*)(uint64_t, uint32_t)>(slots[1]);
        pool.realloc = reinterpret_cast<GameBlock (*)(GameBlock&, uint64_t)>(slots[2]);
        pool.reallocAligned = reinterpret_cast<GameBlock (*)(GameBlock&, uint64_t, uint32_t)>(slots[3]);
        pool.freeFn = reinterpret_cast<void (*)(GameBlock&)>(slots[4]);
    }
    MAC_DIAG( "[vault] ok=%d\n", g_vaultSymbolsOk ? 1 : 0);
    g_vaultReady = true;
}

PoolOps* PoolFor(uintptr_t aTag)
{
    for (auto& pool : g_pools)
    {
        if (pool.tag == aTag)
        {
            return &pool;
        }
    }
    std::fprintf(stderr, "[vault] unknown tag 0x%llx\n", static_cast<unsigned long long>(aTag));
    return nullptr;
}

// RedLib ClassDefinition registers the four classes when kRedLibRegistrars is
// odr-used. RegisterClone is only the fallback when GetClass is still null.
// A parent cycle in a ClassDescriptor walks CClass::GetProperty forever, so
// the R9 diagnostic breaks a chain that repeats or runs past 64.
constexpr uint32_t kClassSize = 0x2D0;

void ZeroDyn(uint8_t* aClass, uint32_t aOffset)
{
    std::memset(aClass + aOffset, 0, 0x10);
}

void ZeroHash(uint8_t* aClass, uint32_t aOffset)
{
    // indexTable through nodeList. The allocator word at +0x28 stays.
    std::memset(aClass + aOffset, 0, 0x28);
}

void DetachContainers(uint8_t* aClass)
{
    ZeroDyn(aClass, 0x28);
    ZeroDyn(aClass, 0x38);
    ZeroDyn(aClass, 0x48);
    ZeroDyn(aClass, 0x58);
    ZeroHash(aClass, 0x78);
    ZeroHash(aClass, 0xA8);
    ZeroHash(aClass, 0xE8);
    ZeroDyn(aClass, 0x118);
    ZeroDyn(aClass, 0x128);
    ZeroDyn(aClass, 0x138);
    ZeroDyn(aClass, 0x148);
    std::memset(aClass + 0x158, 0, 0x28);
    ZeroHash(aClass, 0x180);
    ZeroDyn(aClass, 0x1B0);
    std::memset(aClass + 0x1C0, 0, 256);
    *reinterpret_cast<int16_t*>(aClass + 0x2C0) = -1;
    aClass[0x2C8] = 0;
}

uint32_t g_nextAsyncId = 0;

RED4ext::CBaseRTTIType* TypeByAsyncId(RED4ext::CRTTISystem* aRtti, uint32_t aId)
{
    // Live vtable: GetClass is slot 2 (offset 0x10). GetTypeByAsyncId is slot 1.
    using Fn = RED4ext::CBaseRTTIType* (*)(RED4ext::CRTTISystem*, uint32_t);
    auto** vt = *reinterpret_cast<void***>(aRtti);
    return reinterpret_cast<Fn>(vt[1])(aRtti, aId);
}

[[maybe_unused]] void PrepareAsyncIds(RED4ext::CRTTISystem* aRtti)
{
    if (g_nextAsyncId != 0)
    {
        return;
    }

    auto** vt = *reinterpret_cast<void***>(aRtti);
    MAC_DIAG_RUN(std::cerr << "[TweakXL] rtti " << aRtti << " getById " << vt[1] << " registerFn " << vt[16] << std::endl);

    // The local stand-in counter handed RegisterType ids 1 through 4. Those ids
    // already belong to real types. Async ids are a dense increment, so the
    // first miss is the next free id.
    for (uint32_t id = 1; id < 200000; ++id)
    {
        if ((id % 5000) == 0)
        {
            MAC_DIAG_RUN(std::cerr << "[TweakXL] async alive " << id << std::endl);
        }
        if (TypeByAsyncId(aRtti, id))
        {
            continue;
        }
        if (TypeByAsyncId(aRtti, id + 1) || TypeByAsyncId(aRtti, id + 2) || TypeByAsyncId(aRtti, id + 3))
        {
            MAC_DIAG_RUN(std::cerr << "[TweakXL] async hole at " << id << std::endl);
            continue;
        }
        g_nextAsyncId = id;
        break;
    }

    MAC_DIAG_RUN(std::cerr << "[TweakXL] async next " << g_nextAsyncId << std::endl);
}

void RegisterClone(const char* aName)
{
    MAC_DIAG( "fallback clone %s\n", aName);
    auto* rtti = RED4ext::CRTTISystem::Get();
    const RED4ext::CName name(aName);
    if (rtti->GetClass(name))
    {
        MAC_DIAG_RUN(std::cerr << "[TweakXL] already " << aName << std::endl);
        return;
    }

    auto* donor = rtti->GetClass(RED4ext::CName("IScriptable"));
    if (!donor)
    {
        std::cerr << "[TweakXL] clone missing IScriptable" << std::endl;
        return;
    }

    void* memory = std::aligned_alloc(16, kClassSize);
    if (!memory)
    {
        std::cerr << "[TweakXL] clone alloc failed " << aName << std::endl;
        return;
    }

    std::memcpy(memory, static_cast<const void*>(donor), kClassSize);
    auto* raw = static_cast<uint8_t*>(memory);
    const uint64_t hash = static_cast<uint64_t>(name);
    std::memcpy(raw + 0x18, &hash, sizeof(hash));
    std::memcpy(raw + 0x20, &hash, sizeof(hash));
    std::memcpy(raw + 0x10, &donor, sizeof(donor));
    DetachContainers(raw);

    // IScriptable is abstract. TweakDBBatch is the one concrete class.
    if (std::strcmp(aName, "TweakDBBatch") == 0)
    {
        uint32_t flags = 0;
        std::memcpy(&flags, raw + 0x70, sizeof(flags));
        flags &= ~1u;
        std::memcpy(raw + 0x70, &flags, sizeof(flags));
    }

    const uint32_t asyncId = ++tweakxl_rtti_async_id;
    rtti->RegisterType(reinterpret_cast<RED4ext::CBaseRTTIType*>(memory), asyncId);
    auto* found = rtti->GetClass(name);
    MAC_DIAG_RUN(std::cerr << "[TweakXL] cloned " << aName << " " << found << " id " << asyncId << std::endl);
}

void GuardParent(const char* aName, RED4ext::CClass* aClass)
{
    RED4ext::CClass* seen[64];
    RED4ext::CClass* cursor = aClass;
    for (int step = 0; step < 64; ++step)
    {
        for (int prev = 0; prev < step; ++prev)
        {
            if (seen[prev] == cursor)
            {
                std::fprintf(stderr, "CYCLE %s\n", aName);
                aClass->parent = RED4ext::CRTTISystem::Get()->GetClass(RED4ext::CName("IScriptable"));
                return;
            }
        }
        seen[step] = cursor;
        if (!cursor->parent)
        {
            return;
        }
        cursor = cursor->parent;
    }
    std::fprintf(stderr, "CYCLE %s\n", aName);
    aClass->parent = RED4ext::CRTTISystem::Get()->GetClass(RED4ext::CName("IScriptable"));
}

void LogFuncList(const char* aName, const char* aKind, uint32_t aCount, auto aAt)
{
    MAC_DIAG( "[diag] %s %s", aName, aKind);
    for (uint32_t i = 0; i < aCount; ++i)
    {
        auto* fn = aAt(i);
        const char* text = fn ? fn->shortName.ToString() : nullptr;
        if (text && text[0])
        {
            MAC_DIAG( " %s", text);
        }
        else
        {
            MAC_DIAG( " %llx", static_cast<unsigned long long>(fn ? fn->shortName.hash : 0));
        }
    }
    MAC_DIAG( "\n");
}

void LogR9Class(const char* aName)
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* cls = rtti ? rtti->GetClass(RED4ext::CName(aName)) : nullptr;
    const auto* parent = cls ? cls->parent : nullptr;
    // Diagnostics (TWEAKXL_MAC_DIAG=1 only)
    MAC_DIAG(
                 "[diag] %s ptr=%p name=%llx isAbstract=%u funcs=%u staticFuncs=%u parent=%p parentName=%llx\n",
                 aName,
                 static_cast<void*>(cls),
                 static_cast<unsigned long long>(cls ? cls->name.hash : 0),
                 cls ? static_cast<unsigned>(cls->flags.isAbstract) : 0u,
                 cls ? cls->funcs.size : 0u,
                 cls ? cls->staticFuncs.size : 0u,
                 static_cast<const void*>(parent),
                 static_cast<unsigned long long>(parent ? parent->name.hash : 0));
    if (!cls)
    {
        return;
    }
    LogFuncList(aName, "methods", cls->funcs.size, [&](uint32_t i) { return cls->funcs[i]; });
    LogFuncList(aName, "statics", cls->staticFuncs.size, [&](uint32_t i) { return cls->staticFuncs[i]; });
    GuardParent(aName, cls);
}
}

extern "C" void* tweakxl_memory_vault()
{
    InitVault();
    return &g_vault;
}

extern "C" void tweakxl_vault_alloc(void* aStorage, void* aResult, uint64_t aSize)
{
    InitVault();
    auto* result = static_cast<RED4ext::Memory::AllocationResult*>(aResult);
    result->memory = nullptr;
    result->size = 0;
    auto* pool = PoolFor(reinterpret_cast<uintptr_t>(aStorage));
    if (!pool || !pool->alloc)
    {
        return;
    }
    const GameBlock block = pool->alloc(aSize);
    result->memory = block.memory;
    result->size = block.memory ? block.size : 0;
}

extern "C" void tweakxl_vault_alloc_aligned(void* aStorage, void* aResult, uint64_t aSize, uint32_t aAlign)
{
    InitVault();
    auto* result = static_cast<RED4ext::Memory::AllocationResult*>(aResult);
    result->memory = nullptr;
    result->size = 0;
    auto* pool = PoolFor(reinterpret_cast<uintptr_t>(aStorage));
    if (!pool || !pool->allocAligned)
    {
        return;
    }
    const GameBlock block = pool->allocAligned(aSize, aAlign);
    result->memory = block.memory;
    result->size = block.memory ? block.size : 0;
}

extern "C" void tweakxl_vault_realloc(void* aStorage, void* aResult, void* aInput, uint64_t aSize)
{
    InitVault();
    auto* result = static_cast<RED4ext::Memory::AllocationResult*>(aResult);
    result->memory = nullptr;
    result->size = 0;
    auto* pool = PoolFor(reinterpret_cast<uintptr_t>(aStorage));
    if (!pool || !pool->realloc || !aInput)
    {
        return;
    }
    const GameBlock block = pool->realloc(*static_cast<GameBlock*>(aInput), aSize);
    result->memory = block.memory;
    result->size = block.memory ? block.size : 0;
}

extern "C" void tweakxl_vault_realloc_aligned(void* aStorage, void* aResult, void* aInput, uint64_t aSize, uint32_t aAlign)
{
    InitVault();
    auto* result = static_cast<RED4ext::Memory::AllocationResult*>(aResult);
    result->memory = nullptr;
    result->size = 0;
    auto* pool = PoolFor(reinterpret_cast<uintptr_t>(aStorage));
    if (!pool || !pool->reallocAligned || !aInput)
    {
        return;
    }
    const GameBlock block = pool->reallocAligned(*static_cast<GameBlock*>(aInput), aSize, aAlign);
    result->memory = block.memory;
    result->size = block.memory ? block.size : 0;
}

extern "C" void tweakxl_vault_free(void* aStorage, void* aInput)
{
    InitVault();
    auto* pool = PoolFor(reinterpret_cast<uintptr_t>(aStorage));
    if (!pool || !pool->freeFn || !aInput)
    {
        return;
    }
    pool->freeFn(*static_cast<GameBlock*>(aInput));
}

// R19: the tweakxl_destruct_value_holder stand-in is gone; the hash resolves to the game function 0x22384BC.

extern "C" uintptr_t tweakxl_class_vslot(uint32_t aByteOffset)
{
    static bool ready = false;
    static uintptr_t slots[11] = {};
    static const uint32_t kOffsets[11] = {0x68, 0x70, 0x88, 0x90, 0x98, 0xA0, 0xA8, 0xB8, 0xC8, 0xD0, 0xD8};
    if (!ready)
    {
        auto* rtti = RED4ext::CRTTISystem::Get();
        auto* cls = rtti ? rtti->GetClass(RED4ext::CName("IScriptable")) : nullptr;
        if (!cls)
        {
            std::fprintf(stderr, "[vslot] IScriptable missing\n");
            return 0;
        }
        auto* vt = *reinterpret_cast<uintptr_t**>(cls);
        const uintptr_t base = GameBase();
        const int game = ImageIndex(nullptr, MH_EXECUTE);
        const int self = ImageIndex("TweakXL.dylib", 0);
        MAC_DIAG( "[vslot] vtable=%p\n", static_cast<void*>(vt));
        for (int i = 0; i < 11; ++i)
        {
            const uintptr_t fn = vt[kOffsets[i] / 8];
            const bool inGame = game >= 0 && AddressInImage(static_cast<uint32_t>(game), fn);
            const bool inSelf = self >= 0 && AddressInImage(static_cast<uint32_t>(self), fn);
            MAC_DIAG( "[vslot] off=%x rva=%llx\n", kOffsets[i],
                         static_cast<unsigned long long>(fn - base));
            if (!inGame || inSelf)
            {
                std::fprintf(stderr, "[vslot] reject off=%x inGame=%d inTweakXL=%d\n", kOffsets[i], inGame ? 1 : 0,
                             inSelf ? 1 : 0);
                slots[i] = 0;
            }
            else
            {
                slots[i] = fn;
            }
        }
        ready = true;
    }
    for (int i = 0; i < 11; ++i)
    {
        if (kOffsets[i] == aByteOffset)
        {
            return slots[i];
        }
    }
    std::fprintf(stderr, "[vslot] unknown offset %x\n", aByteOffset);
    return 0;
}

extern "C" __attribute__((visibility("default"))) void tweakxl_registerNatives()
{
    static bool once = false;
    if (once)
    {
        return;
    }
    once = true;

    MAC_DIAG_RUN(std::cerr << "[TweakXL] tweakxl_registerNatives enter" << std::endl);
    try
    {
        InitVault();
        // Diagnostics (TWEAKXL_MAC_DIAG=1 only)
        MAC_DIAG( "[diag] pending register %zu describe %zu registrars %zu\n",
                     Red::TypeInfoRegistrar::PendingRegisterCount(),
                     Red::TypeInfoRegistrar::PendingDescribeCount(),
                     sizeof(kRedLibRegistrars) / sizeof(kRedLibRegistrars[0]));
        Red::TypeInfoRegistrar::RunPending();
        LogR9Class("ScriptableTweak");
        LogR9Class("TweakDBManager");
        LogR9Class("TweakDBBatch");
        LogR9Class("TweakXL");
        LogR9Class("gamedataTweakDBInterface");
        auto* rtti = RED4ext::CRTTISystem::Get();
        for (const char* name : {"ScriptableTweak", "TweakDBBatch", "TweakDBManager", "TweakXL"})
        {
            if (!rtti->GetClass(RED4ext::CName(name)))
            {
                RegisterClone(name);
            }
        }
        MAC_DIAG_RUN(std::cerr << "[TweakXL] tweakxl_registerNatives leave" << std::endl);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[TweakXL] tweakxl_registerNatives exception " << ex.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[TweakXL] tweakxl_registerNatives exception" << std::endl;
    }
}
#endif // __APPLE__
