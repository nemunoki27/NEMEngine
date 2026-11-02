include "premakeCommon.lua"

workspace "NEMEngine"
    location (NEM_PROJECT_ROOT)
    configurations { "Debug", "Develop", "Release" }
    platforms { "x64" }
    startproject "NEMEditor"

    filter "platforms:x64"
        architecture "x64"
    filter {}

    NEM_ConfigureWorkspaceLayout(NEM_PROJECT_ROOT)

group "Externals"
    include "premakeExternals.lua"
group ""

include "premakeNemengine.lua"
include "sandbox.lua"

group "Launcher"
for _, name in ipairs({ "NEM.Distribution", "NEM.ProjectTool", "NEM.Launcher", "NEM.Distribution.Tests" }) do
    externalproject (name)
        location (path.join(NEM_PROJECT_ROOT, "Tools", name))
        filename (name)
        kind "SharedLib"
        language "C#"
end
group ""
