#pragma once

// The Windows builds use psiberx's RED4ext.SDK 1.0, which names the plugin API RED4ext::v1 (RED4ext::v1::Sdk,
// RED4ext::v1::PluginHandle, RED4EXT_V1_SEMVER and so on). The macOS SDK port is still on the 0.5 API, where the same
// structures have no v1 namespace. The layouts are the same, and a 1.0 plugin reports API version 0 on both
// (RED4EXT_API_VERSION_1_COMPAT_0). These aliases let the shared sources keep the v1 names unchanged.

#if defined(__APPLE__)

#include <RED4ext/Api/EMainReason.hpp>
#include <RED4ext/Api/PluginHandle.hpp>
#include <RED4ext/Api/Runtime.hpp>
#include <RED4ext/Api/Sdk.hpp>
#include <RED4ext/Api/SemVer.hpp>
#include <RED4ext/Api/Version.hpp>

namespace RED4ext::v1
{
using PluginHandle = ::RED4ext::PluginHandle;
using Sdk = ::RED4ext::Sdk;
using PluginInfo = ::RED4ext::PluginInfo;
using EMainReason = ::RED4ext::EMainReason;
using SemVer = ::RED4ext::SemVer;
using FileVer = ::RED4ext::FileVer;
using Logger = ::RED4ext::Logger;
using Hooking = ::RED4ext::Hooking;
using GameStates = ::RED4ext::GameStates;
using GameState = ::RED4ext::GameState;
using Scripts = ::RED4ext::Scripts;
} // namespace RED4ext::v1

#ifndef RED4EXT_V1_SEMVER
#define RED4EXT_V1_SEMVER(major, minor, patch) RED4EXT_V0_SEMVER(major, minor, patch)
#endif

#ifndef RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT
#define RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT RED4EXT_V0_RUNTIME_INDEPENDENT
#endif

#ifndef RED4EXT_V1_SDK_VERSION_1_0_0_COMPAT_0_5_0
#define RED4EXT_V1_SDK_VERSION_1_0_0_COMPAT_0_5_0 RED4EXT_V0_SEMVER(0, 5, 0)
#endif

#ifndef RED4EXT_API_VERSION_1_COMPAT_0
#define RED4EXT_API_VERSION_1_COMPAT_0 RED4EXT_API_VERSION_0
#endif

#endif
