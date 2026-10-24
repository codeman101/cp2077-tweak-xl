#if defined(__APPLE__) // macOS only: the address table of the Mac game
#include "TweakXLAddressResolver.hpp"
#include <iostream>
#include <cstdlib>
#include <RED4ext/Relocation.hpp>

namespace Support
{

std::unordered_map<uint32_t, int> TweakXLAddressResolver::s_requestedAddresses;

namespace
{
bool IsAddressTraceEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("TWEAKXL_ADDR_TRACE");
        return value && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}
}

TweakXLAddressResolver::TweakXLAddressResolver()
{
    InitializeAddressTable();
}

void TweakXLAddressResolver::OnInitialize()
{
    // Set this resolver as the default for Core::AddressResolver
    AddressResolver::SetDefault(*this);
    if (IsAddressTraceEnabled())
    {
        std::cerr << "[TweakXLAddressResolver] Registered as default address resolver" << std::endl;
    }
}

uintptr_t TweakXLAddressResolver::GetImageBase()
{
    static const uintptr_t base = RED4ext::RelocBase::GetImageBase();
    return base;
}

void TweakXLAddressResolver::InitializeAddressTable()
{
    // =========================================================================
    // TweakXL Required Addresses (from src/Red/Addresses/Library.hpp)
    // =========================================================================
    // The functions TweakXL needs to operate. Offsets are relative to the image base (0x100000000).
    // The evidence for each row is in tools/addr-match (check-rows.py, check-rows.reviewed.tsv).
    // =========================================================================
    
    // Main function (hash: 240386859 = 0x0E54032B)
    // CONFIRMED: Entry point from Mach-O LC_MAIN
    m_addressTable[240386859] = 0x323A0;
    
    // TweakDB_Init (hash: 3062572522). Calls GetTweakDB, then LoadOptimized.
    // 0x2B79AC0 is a script native.
    m_addressTable[3062572522] = 0x1EF64C8;
    m_addressTable[4053739225] = 0x1EF64C8; // 2.5 hash (psiberx/cp2077-tweak-xl#22), same function
    
    // TweakDB_Load (hash: 3602585178 = 0xD6B1DB5A)
    // Opens tweakdb_ep1.bin. Two args: TweakDB* in x0, path in x1.
    // Old NCC value 0x2B7BE94 is not on that backtrace.
    m_addressTable[3602585178] = 0x37CE05C;
    // 2.5 hash (PR #22). Mac 2.5 signature: x0 TweakDB*, w1 bool (Init passes 1), x2 path.
    m_addressTable[63182599] = 0x37CE05C;
    
    // TweakDB_TryLoad (hash: 3512345737 = 0xD16A2999)
    // 4-arg bool. x1 is the TweakDB, x2 is the path. On the open backtrace
    // of r6/cache/tweakdb_ep1.bin. Old NCC value 0x2B7BAB0 is never called.
    m_addressTable[3512345737] = 0x37D60CC;
    // 2.5 hash (PR #22). Mac 2.5 signature: x0 a1, x1 TweakDB*, x2 path, x3 blob, w4 bool (5th arg, a register on arm64).
    m_addressTable[464332086] = 0x37D60CC;
    
    // TweakDB_CreateRecord (hash: 838931066). game::data::AddRecord.
    // 0x2B737AC registers the TweakDBRecord RTTI class.
    m_addressTable[838931066] = 0x3309CC4;

    // TweakDB_Get (hash: 914361828).
    m_addressTable[914361828] = 0x37CC900;

    // CRTTISystem_Get. Same function the probe uses.
    m_addressTable[1247874916] = 0x10B3CAC;

    // CString and DynArray. Real Mac bodies, not local stand-ins.
    m_addressTable[3357477547] = 0x2CCAC;
    m_addressTable[2065762423] = 0x2933C;
    m_addressTable[3904113489] = 0x2AA74; // CString_copy: the copy assign (Windows 0x2503FC). R25: was the copy constructor 0x2B110.
    m_addressTable[1409615660] = 0x2914C;
    m_addressTable[2057311186] = 0x28674;

    // CNamePool. AddCString is implemented as Add(c_str()).
    m_addressTable[1759447004] = 0x4E38940;
    m_addressTable[10488987] = 0x4E389D0;
    m_addressTable[3649309656] = 0x4E38978;

    // Handle_DecWeakRef (hash: 859509764). void(SharedPtrBase*). Identical copy at 0x914668.
    m_addressTable[859509764] = 0x1025DC8;
    // ResourceDepot (hash: 1704595399). Global ResourceDepot* (data, not code). Accessor 0x21C6820.
    m_addressTable[1704595399] = 0x805A7D8;
    m_addressTable[1518341917] = 0x10C36A0;  // CClass_CreateInstance (rtti::ClassType::CreateObject)
    m_addressTable[404169501] = 0x1099fbc;   // CBaseFunction_InternalExecute
    m_addressTable[882511585] = 0x78300D8;   // CStack_vtbl (data: vptr value, slot 0)
    m_addressTable[3689484881] = 0x10B0B44;  // ISerializable_sub_C0 (Mac vtable slot +0xC8, base of 6,482 vtables; 2.3.1 0x2186208). 2.5 calls it from the script context printer 0x11EF228
    m_addressTable[3791200470] = 0x1098FC0;  // CBaseFunction_ExecuteScripted (Gate 5 path)
    m_addressTable[3121353053] = 0x1025c8c;  // Handle_ctor (Gate 5 path)
    m_addressTable[961751128] = 0x9A5C940;   // OpcodeHandlers (data: handler table; Gate 5 path)
    m_addressTable[2920426135] = 0x109b974;  // CClassStaticFunction_ctor
    m_addressTable[1613572595] = 0x109b8a4;  // CClassFunction_ctor (Mac pointer-to-member ABI)
    m_addressTable[4201325776] = 0x109BAA8;  // CGlobalFunction_ctor
    
    // StatsDataSystem_InitializeRecords (hash: 1299190886 = 0x4D6E8066)
    // DISCOVERED: Medium function at 0x3a939a0 that works with TweakDB records (offset 0xD8)
    // Matches initialization pattern for stat records
    m_addressTable[1299190886] = 0x23B2F68;
    
    // StatsDataSystem_InitializeParams (hash: 3652194890 = 0xD9B5924A)
    // DISCOVERED: Large function at 0x3a932a8 working with both records (0xD8) and params (0xE8)
    // Called to initialize stat parameter arrays
    m_addressTable[3652194890] = 0x23B29A0;
    
    // StatsDataSystem_GetStatRange (hash: 1444748215 = 0x5620D3B7). A virtual (Mac vtable slot 0x208).
    // Mac signature (R23, differs from Windows): {float min, float max} (*)(void* system, uint32_t stat), the two floats come back in s0:s1.
    m_addressTable[1444748215] = 0x23B3CB0;
    
    // StatsDataSystem_GetStatFlags (hash: 3123320294 = 0xBA1CE5E6)
    // DISCOVERED: Small function at 0x3a93ef0, returns W0 (32-bit flags)
    // Signature: uint32_t (*)(void*, uint32_t)
    m_addressTable[3123320294] = 0x23B34B4;
    
    // StatsDataSystem_CheckStatFlag (hash: 2954893634 = 0xB01D2542)
    // DISCOVERED: Small function at 0x3a93e74 with comparison and conditional return
    // Signature: bool (*)(void*, uint32_t, uint32_t)
    m_addressTable[2954893634] = 0x23B3438;
    
    // =========================================================================
    // Additional SDK addresses that may be needed
    // These are from RED4ext.SDK AddressHashes.hpp
    // =========================================================================
    
    if (IsAddressTraceEnabled())
    {
        std::cerr << "[TweakXLAddressResolver] Initialized with " << m_addressTable.size()
                  << " address mappings" << std::endl;
    }
}

uintptr_t TweakXLAddressResolver::ResolveAddress(uint32_t aAddressID)
{
    // Track requested addresses for debugging
    s_requestedAddresses[aAddressID]++;
    
    auto it = m_addressTable.find(aAddressID);
    if (it != m_addressTable.end())
    {
        uintptr_t offset = it->second;
        
        if (offset == 0)
        {
            // Row without a Mac address: log a warning
            std::cerr << "[TweakXLAddressResolver] WARNING: Address 0x" << std::hex << aAddressID 
                      << std::dec << " (" << aAddressID << ") has no Mac address (offset 0x0)" << std::endl;
            return 0;
        }
        
        uintptr_t resolved = GetImageBase() + offset;
        if (IsAddressTraceEnabled())
        {
            std::cerr << "[TweakXLAddressResolver] Resolved 0x" << std::hex << aAddressID << " -> 0x" << resolved
                      << std::dec << std::endl;
        }
        return resolved;
    }
    
    // Address not in our table - log error
    std::cerr << "[TweakXLAddressResolver] ERROR: Unknown hash 0x" << std::hex << aAddressID
              << std::dec << " (" << aAddressID << ")" << std::endl;
    std::cerr << "  This address needs to be added to TweakXLAddressResolver" << std::endl;
    
    return 0;
}

} // namespace Support
#endif // __APPLE__
