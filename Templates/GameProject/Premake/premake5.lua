newoption {
    trigger = "engine-root",
    value = "PATH",
    description = "Path to NEMEngine root"
}

local GAME_ROOT = path.getabsolute(path.join(_SCRIPT_DIR, ".."))
local localSettings = path.join(_SCRIPT_DIR, "local_settings.lua")

if os.isfile(localSettings) then
    dofile(localSettings)
end

GAME_NAME = GAME_NAME or "__GAME_NAME__"

if _OPTIONS["engine-root"] then
    NEMENGINE_ROOT = path.getabsolute(_OPTIONS["engine-root"])
end

if not NEMENGINE_ROOT then
    local bundledEngine = path.join(GAME_ROOT, "External/NEMEngine")
    if os.isdir(bundledEngine) then
        NEMENGINE_ROOT = path.getabsolute(bundledEngine)
    end
end

if not NEMENGINE_ROOT or not os.isdir(NEMENGINE_ROOT) then
    error("NEMENGINE_ROOT was not found. Set Premake/local_settings.lua, or place the engine at External/NEMEngine, or pass --engine-root=...")
end

NEM_BUILD_ROOT = GAME_ROOT
NEM_GENERATED_ROOT = path.join(GAME_ROOT, "Generated")
NEM_ENGINE_PROJECT_LOCATION = path.join(NEMENGINE_ROOT, "Project")

dofile(path.join(NEMENGINE_ROOT, "Premake/premakeCommon.lua"))

workspace (GAME_NAME)
    location (path.join(GAME_ROOT, "Project"))
    configurations { "Debug", "Develop", "Release" }
    platforms { "x64" }
    startproject (GAME_NAME)

    filter "platforms:x64"
        architecture "x64"
    filter {}

    NEM_ConfigureWorkspaceOutputLayout(path.join(GAME_ROOT, "Project"))

group "Externals"
    include(path.join(NEMENGINE_ROOT, "Premake/premakeExternals.lua"))
group ""

include(path.join(NEMENGINE_ROOT, "Premake/premakeNemengine.lua"))

project (GAME_NAME)
    location (path.join(GAME_ROOT, "Project"))
    kind "WindowedApp"

    NEM_ApplyDefaultCppSettings()
    NEM_AddAppProjectFiles(path.join(GAME_ROOT, "Project"), GAME_NAME)

    includedirs {
        path.join(GAME_ROOT, "Project"),
        path.join(GAME_ROOT, "Project", GAME_NAME),
    }

    NEM_AddEngineRuntimeLinkSettings()
    NEM_ApplyDefaultConfigFilters()