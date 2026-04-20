include "premakeCommon.lua"

workspace "NEMEngine"
    location (NEM_PROJECT_ROOT)
    configurations { "Debug", "Develop", "Release" }
    platforms { "x64" }
    startproject "Sandbox"

    filter "platforms:x64"
        architecture "x64"
    filter {}

    NEM_ConfigureWorkspaceLayout(NEM_PROJECT_ROOT)

group "Externals"
    include "premakeExternals.lua"
group ""

include "premakeNemengine.lua"
include "sandbox.lua"