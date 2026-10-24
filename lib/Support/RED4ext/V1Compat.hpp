#pragma once

// The Windows RED4ext SDK and the current macOS SDK both have the v1 plugin API
// (RED4ext::v1::PluginHandle, RED4ext::v1::Sdk, RED4EXT_V1_SEMVER and so on). This header
// used to alias those names onto the v0 types and to define the v1 macros from the v0
// ones. The aliases and the macro shims are gone; the v1 headers are native.

#if defined(__APPLE__)

#include <RED4ext/Api/ApiVersion.hpp>
#include <RED4ext/Api/v1/EMainReason.hpp>
#include <RED4ext/Api/v1/FileVer.hpp>
#include <RED4ext/Api/v1/GameState.hpp>
#include <RED4ext/Api/v1/GameStates.hpp>
#include <RED4ext/Api/v1/Hooking.hpp>
#include <RED4ext/Api/v1/Logger.hpp>
#include <RED4ext/Api/v1/PluginHandle.hpp>
#include <RED4ext/Api/v1/PluginInfo.hpp>
#include <RED4ext/Api/v1/Runtime.hpp>
#include <RED4ext/Api/v1/Scripts.hpp>
#include <RED4ext/Api/v1/Sdk.hpp>
#include <RED4ext/Api/v1/SemVer.hpp>
#include <RED4ext/Api/v1/Version.hpp>

#endif
