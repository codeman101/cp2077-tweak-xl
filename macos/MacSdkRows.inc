// R23. Mac RVAs for SDK address hashes that the sibling SDK's own table (RED4ext.SDK-macos, RED4ext/Relocation-inl.hpp) does not have and that
// TweakXL needs: the real IScriptable_DestructValueHolder, and the job runtime for the async commit of a tweak changeset (the same rows
// Codeware verified in R18; matched by read-only agents and rated by tools/addr-match/check-rows.py). CMakeLists.txt injects these rows into a
// generated copy of the SDK header, which comes first on the include path; the SDK's own UniversalRelocFunc calls resolve through it.
// Format: { hash, RVA },  // name. RVA is relative to the Cyberpunk2077 image base.
        { 2627471740, 0x4DDF868 },   // JobHandle_Join. void(JobHandle*, const JobHandle&). Windows 0x142838.
        { 2655521326, 0x4DDF828 },   // JobHandle_dtor (ReleaseInternalHandle). Mac returns this. The caller-less twins are 0x9D498C and 0x9D49C4. Windows 0x1419A4.
        { 1862212562, 0x4DE2D7C },   // JobInternalHandle_Acquire. Mac JobInternalHandle*(ignored, const char* name, uintptr_t unk): one more argument than the SDK. Windows 0x142B20.
        { 242552139,  0x4DDF1C4 },   // JobQueue_ctor_FromGroup (job::Builder::Builder). JobQueue*(JobQueue*, const JobGroup&). Windows 0x142634.
        { 2193429752, 0x4DDEF64 },   // JobQueue_ctor_FromParams. JobQueue*(JobQueue*, uint8_t, uint8_t, uint64_t). Windows 0x218B54.
        { 69014212,   0x4DDF3C4 },   // JobQueue_Capture. Mac JobHandle(JobQueue*): the handle comes back through x8, no out pointer. Windows 0x218C38.
        { 1525942140, 0x4DDF488 },   // JobQueue_dtor. Body. The game calls it through the thunk 0x9D46EC. Windows 0x142F88.
        { 3651996672, 0x4DDF354 },   // JobQueue_SyncWait. void(JobQueue*). Windows 0x141B10.
        { 2621709954, 0x4DE3088 },   // JobDispatcher_DispatchJob. uint32_t(dispatcher, const JobInstance&, uint8_t, JobInternalHandle*, JobInternalHandle*). Not its tail target 0x9D8720. Windows 0x14299C.
        { 1508445968, 0x9D0E550 },  // JobDispatcher. Data: the cell that holds the dispatcher pointer. Windows .data 0x3438980.
        { 0xC0DE0010, 0x4DF0D14 },   // MacJobSetLocalThreadParam. void(uint8_t). Mac-only id: the SDK writes TLS::Get()->jobParam, and TLS::Get() is null on Mac. 2468 callers, all around job bodies.
        { 55710513,   0x11E4964 },  // IScriptable_DestructValueHolder. void(IScriptable*). Replaces the TweakXL stand-in. Windows 0x14934C.
