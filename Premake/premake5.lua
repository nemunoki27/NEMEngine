include "premakeCommon.lua"

workspace "NEMEngine"
    location (path.join(NEMENGINE_ROOT, "Project"))
    configurations { "Debug", "Develop", "Release" }
    platforms { "x64" }
    startproject "Sandbox"

    filter "platforms:x64"
        architecture "x64"
    filter {}

    objdir (path.join(NEMENGINE_ROOT, "Generated/Intermediate/%{prj.name}/%{cfg.buildcfg}"))

    filter "kind:StaticLib"
        targetdir (path.join(NEMENGINE_ROOT, "Generated/Bin/%{cfg.buildcfg}/%{prj.name}"))

    filter "kind:ConsoleApp or kind:WindowedApp"
        targetdir (path.join(NEMENGINE_ROOT, "Generated/Output/%{cfg.buildcfg}/%{prj.name}"))
        debugdir  (path.join(NEMENGINE_ROOT, "Project"))

    filter {}

group "Externals"
    include "premakeExternals.lua"
group ""

include "premakeNemengine.lua"
include "sandbox.lua"