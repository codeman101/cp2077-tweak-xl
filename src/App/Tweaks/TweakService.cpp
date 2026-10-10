#include "TweakService.hpp"
#if defined(__APPLE__)
#include "MacDiag.hpp"
#include "Red/StatsDataSystem.hpp"
#endif
#include "App/Tweaks/Declarative/TweakImporter.hpp"
#include "App/Tweaks/Executable/TweakExecutor.hpp"
#include "App/Tweaks/Metadata/MetadataExporter.hpp"
#include "App/Tweaks/Metadata/MetadataImporter.hpp"
#include "Red/TweakDB/Raws.hpp"

#include <cstring>
#include <cstdio>
#include <exception>
#include <chrono>
#include <thread>

App::TweakService::TweakService(const Core::SemvVer& aProductVer, std::filesystem::path aGameDir,
                                std::filesystem::path aTweaksDir, std::filesystem::path aInheritanceMapPath,
                                std::filesystem::path aExtraFlatsPath, std::filesystem::path aSourcesDir)
    : m_gameDir(std::move(aGameDir))
    , m_tweaksDir(std::move(aTweaksDir))
    , m_sourcesDir(std::move(aSourcesDir))
    , m_inheritanceMapPath(std::move(aInheritanceMapPath))
    , m_extraFlatsPath(std::move(aExtraFlatsPath))
    , m_productVer(aProductVer)
{
    m_importPaths.push_back(m_tweaksDir);
}

#if defined(__APPLE__)
void App::TweakService::OnBootstrap()
{
    CreateTweaksDir();

    auto hookTryLoad = HookAfter<Raw::TryLoadTweakDB>([&](bool& aSuccess, void*, Red::TweakDB* aDb, Red::CString& aPath, void*) {
        LogInfo("TryLoadTweakDB returned {} db={}.", aSuccess ? "true" : "false", static_cast<void*>(aDb));
        if (aSuccess && aDb)
        {
            const char* path = aPath.c_str();
            const bool episode = path && std::strstr(path, "tweakdb_ep1") != nullptr;
            m_reflection = Core::MakeShared<Red::TweakDBReflection>(aDb);
            m_manager = Core::MakeShared<Red::TweakDBManager>(m_reflection);
            m_context = Core::MakeShared<App::TweakContext>(m_productVer, episode);
            m_importer = Core::MakeShared<App::TweakImporter>(m_manager, m_context);
            m_executor = Core::MakeShared<App::TweakExecutor>(m_manager);
            m_changelog = Core::MakeShared<App::TweakChangelog>();

            ReportUsage();

            if (ImportMetadata())
            {
                EnsureRuntimeAccess();
                ApplyPatches();
                LoadTweaks();
                ReportUsage();
            }
        }
    });

    hookTryLoad.OrThrow("Failed to hook TryLoadTweakDB.");

    auto hookInit = HookAfter<Raw::InitTweakDB>([&]() {
        EnsureRuntimeAccess();
        CheckForIssues();
#if defined(__APPLE__)
        // R11 self-test (TWEAKXL_MAC_DIAG=1 only): static call, construct, member call, destruct.
        if (Mac::DiagEnabled())
        try
        {
            Red::CString version;
            const bool versionOk = Red::CallStatic("TweakXL", "Version", version);
            MAC_DIAG("[selftest] version ok=%d text=%s\n", versionOk ? 1 : 0,
                         version.c_str() ? version.c_str() : "");

            Red::Handle<Red::IScriptable> batch;
            const bool batchOk = Red::CallStatic("TweakDBManager", "StartBatch", batch);
            unsigned long long hash = 0;
            if (batch)
            {
                auto* type = batch->GetType();
                if (type)
                {
                    hash = type->GetName().hash;
                }
            }
            MAC_DIAG("[selftest] batch ok=%d nonnull=%d hash=%llx\n", batchOk ? 1 : 0, batch ? 1 : 0, hash);

            const bool commitOk = Red::CallVirtual(batch, "Commit");
            MAC_DIAG("[selftest] commit returned=%d\n", commitOk ? 1 : 0);

            batch.Reset();
            MAC_DIAG("[selftest] released\n");

            // R23 self-test, read only: every BaseStats.* record the tweaks added is readable now (menu time, before any game session
            // creates the StatsDataSystem), with its enumName flat and its record type.
            if (m_manager && m_changelog)
            {
                int statRecordsSeen = 0;
                for (const auto& recordID : m_changelog->GetAffectedRecords())
                {
                    const auto name = m_manager->GetName(recordID);
                    if (!name.starts_with("BaseStats."))
                        continue;

                    ++statRecordsSeen;
                    const auto record = m_manager->GetRecord(recordID);
                    auto enumNameFlat = m_manager->GetFlat({recordID, ".enumName"});
                    const char* enumName = enumNameFlat ? enumNameFlat.As<Red::CString>().c_str() : nullptr;
                    const char* typeName = record && record->GetType() ? record->GetType()->GetName().ToString() : nullptr;
                    const bool nameMatches = enumName && name.substr(10) == enumName;
                    MAC_DIAG("[selftest] stat record %.*s found=%d type=%s enumName=%s match=%d enumValue=%u %s\n",
                             static_cast<int>(name.size()), name.data(), record ? 1 : 0, typeName ? typeName : "?",
                             enumName ? enumName : "?", nameMatches ? 1 : 0,
                             record ? static_cast<uint32_t>(Raw::StatRecord::EnumValue::Ref(record)) : 0u,
                             (record && nameMatches) ? "PASS" : "FAIL");
                }
                MAC_DIAG("[selftest] stat records seen=%d\n", statRecordsSeen);
            }

            // R25 self-test: the flat buffer after the last flat is zero. SDK UpsizeFlatDataBuffer zeroes the tail it adds, and
            // TweakDBBuffer::SyncBufferData reads a zero qword as the alignment pad before a 16-byte aligned flat.
            if (m_manager)
            {
                const auto* db = m_manager->GetTweakDB();
                if (db && db->flatDataBuffer && db->flatDataBufferEnd >= db->flatDataBuffer)
                {
                    const uintptr_t used = db->flatDataBufferEnd - db->flatDataBuffer;
                    const uintptr_t capacity = db->flatDataBufferCapacity;
                    const uintptr_t room = capacity > used ? capacity - used : 0;
                    const uintptr_t scan = room < (4u << 20) ? room : (4u << 20);
                    const auto* tail = reinterpret_cast<const uint64_t*>(db->flatDataBufferEnd);
                    size_t nonzero = 0;
                    for (uintptr_t i = 0; i < scan / 8; ++i)
                        nonzero += tail[i] != 0 ? 1 : 0;
                    MAC_DIAG("[selftest] R25 flat buffer tail: used=%lu capacity=%lu scanned=%lu bytes nonzero qwords=%zu %s\n",
                             static_cast<unsigned long>(used), static_cast<unsigned long>(capacity),
                             static_cast<unsigned long>(scan), nonzero, (scan > 0 && nonzero == 0) ? "PASS" : "FAIL");
                }
            }

            // R26: the call count of every attached hook, 45 s later (diagnostics only).
            std::thread([] {
                std::this_thread::sleep_for(std::chrono::seconds(45));
                Core::HookHits::Dump("txl", "t+45s", Core::Platform::GetImageBase());
            }).detach();
        }
        catch (const std::exception& ex)
        {
            MAC_DIAG("[selftest] exception %s\n", ex.what());
        }
        catch (...)
        {
            MAC_DIAG("[selftest] exception\n");
        }
#endif
    });
    hookInit.OrThrow("Failed to hook InitTweakDB.");
}

void App::TweakService::OnShutdown()
{
    // Detach hooks first to avoid callbacks into partially torn-down state.
    Unhook<Raw::TryLoadTweakDB>();
    Unhook<Raw::InitTweakDB>();

    // Release runtime state.
    m_executor.reset();
    m_importer.reset();
    m_changelog.reset();
    m_context.reset();
    m_manager.reset();
    m_reflection.reset();
}
#else
void App::TweakService::OnBootstrap()
{
    CreateTweaksDir();

    HookAfter<Raw::TryLoadTweakDB>([&](bool& aSuccess) {
        if (aSuccess)
        {
            m_reflection = Core::MakeShared<Red::TweakDBReflection>();
            m_manager = Core::MakeShared<Red::TweakDBManager>(m_reflection);
            m_context = Core::MakeShared<App::TweakContext>(m_productVer);
            m_importer = Core::MakeShared<App::TweakImporter>(m_manager, m_context);
            m_executor = Core::MakeShared<App::TweakExecutor>(m_manager);
            m_changelog = Core::MakeShared<App::TweakChangelog>();

            ReportUsage();

            if (ImportMetadata())
            {
                EnsureRuntimeAccess();
                ApplyPatches();
                LoadTweaks();
                ReportUsage();
            }
        }
    });

    HookAfter<Raw::InitTweakDB>([&]() {
        EnsureRuntimeAccess();
        CheckForIssues();
    });
}
#endif

void App::TweakService::LoadTweaks()
{
    if (m_manager)
    {
        m_importer->ImportTweaks(m_importPaths, m_changelog);
        m_executor->ExecuteTweaks();
    }
}

void App::TweakService::ImportTweaks()
{
    if (m_manager)
    {
        m_importer->ImportTweaks(m_importPaths, m_changelog);
    }
}

void App::TweakService::ExecuteTweaks()
{
    if (m_manager)
    {
        m_executor->ExecuteTweaks();
    }
}

void App::TweakService::ExecuteTweak(Red::CName aName)
{
    if (m_manager)
    {
        m_executor->ExecuteTweak(aName);
    }
}

void App::TweakService::EnsureRuntimeAccess()
{
    if (m_manager)
    {
        m_manager->GetTweakDB()->unk160 = 0;
    }
}

void App::TweakService::ApplyPatches()
{
    if (m_manager)
    {
        m_manager->CloneRecord("Vendors.IsPresent", "Vendors.Always_Present");
        m_manager->RegisterName("Vendors.IsPresent");
    }
}

void App::TweakService::CheckForIssues()
{
    if (m_manager && m_changelog)
    {
        m_changelog->CheckForIssues(m_manager);
    }
}

void App::TweakService::ReportUsage()
{
    if (m_manager)
    {
        auto stats = m_manager->GetBuffer()->GetStats();

        LogDebug("TweakDB contains {} records and {} flats.", stats.recordEntries, stats.flatEntries);
        LogDebug("TweakDB buffer is used at {:.3f} MiB / {:.0f} MiB ({:.1f}%) with {} pooled values.",
                 static_cast<float>(stats.bufferSize) / (1024 * 1024),
                 static_cast<float>(stats.bufferMaxSize) / (1024 * 1024),
                 static_cast<float>(stats.bufferSize) / static_cast<float>(stats.bufferMaxSize) * 100.0f,
                 stats.poolValues);
    }
}

void App::TweakService::CreateTweaksDir()
{
    std::error_code error;

    if (!std::filesystem::exists(m_tweaksDir, error))
    {
        if (!std::filesystem::create_directories(m_tweaksDir, error))
        {
            LogWarning("Cannot create tweaks directory \"{}\": {}.",
                       std::filesystem::relative(m_tweaksDir, m_gameDir).string(), error.message());
        }
    }
}

bool App::TweakService::RegisterTweak(std::filesystem::path aPath)
{
    std::error_code error;

    if (aPath.is_relative())
    {
        aPath = m_gameDir / aPath;
    }

    if (!std::filesystem::exists(aPath, error) || !std::filesystem::is_regular_file(aPath, error))
    {
        LogError("Can't register non-existing tweak \"{}\".",
                 std::filesystem::relative(aPath, m_gameDir).string());
        return false;
    }

    m_importPaths.emplace_back(std::move(aPath));
    return true;
}

bool App::TweakService::RegisterDirectory(std::filesystem::path aPath)
{
    std::error_code error;

    if (aPath.is_relative())
    {
        aPath = m_gameDir / aPath;
    }

    if (!std::filesystem::exists(aPath, error) || !std::filesystem::is_directory(aPath, error))
    {
        LogError("Can't register non-existing tweak directory \"{}\".",
                 std::filesystem::relative(aPath, m_gameDir).string());
        return false;
    }

    m_importPaths.emplace_back(std::move(aPath));
    return true;
}

bool App::TweakService::ImportMetadata()
{
    MetadataImporter importer{m_manager};

    LogInfo("Loading inheritance metadata...");

    if (!importer.ImportInheritanceMap(m_inheritanceMapPath))
    {
        LogError("Can't load inheritance metadata from \"{}\".", m_inheritanceMapPath.string());
        return false;
    }

    LogInfo("Loading extra flats metadata...");

    if (!importer.ImportExtraFlats(m_extraFlatsPath))
    {
        LogError("Can't load extra flats metadata from \"{}\".", m_extraFlatsPath.string());
        return false;
    }

    return true;
}

void App::TweakService::ExportMetadata()
{
    MetadataExporter exporter{m_manager};
    exporter.LoadSource(m_sourcesDir);
    exporter.ExportInheritanceMap(m_inheritanceMapPath);
    exporter.ExportExtraFlats(m_extraFlatsPath);
    exporter.ExportInheritanceMap(m_inheritanceMapPath.replace_extension(".yaml"));
    exporter.ExportExtraFlats(m_extraFlatsPath.replace_extension(".yaml"));
}

Red::TweakDBManager& App::TweakService::GetManager()
{
    return *m_manager;
}

Red::TweakDBReflection& App::TweakService::GetReflection()
{
    return *m_reflection;
}

App::TweakChangelog& App::TweakService::GetChangelog()
{
    return *m_changelog;
}
