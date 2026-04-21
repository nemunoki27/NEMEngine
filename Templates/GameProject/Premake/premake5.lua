newoption {
    trigger = "engine-root",
    value = "PATH",
    description = "Path to NEMEngine root"
}

local GAME_ROOT = path.getabsolute(path.join(_SCRIPT_DIR, ".."))
local GAME_PROJECT_ROOT = path.join(GAME_ROOT, "Project")
local localSettings = path.join(_SCRIPT_DIR, "local_settings.lua")

if os.isfile(localSettings) then
    dofile(localSettings)
end

GAME_NAME = GAME_NAME or "__GAME_NAME__"

if _OPTIONS["engine-root"] then
    NEMENGINE_ROOT = path.getabsolute(_OPTIONS["engine-root"])
end

if not NEMENGINE_ROOT then
    local bundledEngine = path.join(GAME_ROOT, "External", "NEMEngine")
    if os.isdir(bundledEngine) then
        NEMENGINE_ROOT = path.getabsolute(bundledEngine)
    end
end

if not NEMENGINE_ROOT or not os.isdir(NEMENGINE_ROOT) then
    error("NEMENGINE_ROOT was not found. Set Premake/local_settings.lua, or place the engine at External/NEMEngine, or pass --engine-root=...")
end

dofile(path.join(NEMENGINE_ROOT, "Premake", "premakeCommon.lua"))

NEM_OUTPUT_ROOT = path.join(GAME_ROOT, "Generated")
NEM_GENERATED_ROOT = NEM_OUTPUT_ROOT
NEM_USE_EXTERNAL_ENGINE_PROJECT = true
NEM_IMGUI_PROJECT_LOCATION = path.join(GAME_PROJECT_ROOT, "Externals/imgui")
NEM_MESHOPTIMIZER_PROJECT_LOCATION = path.join(GAME_PROJECT_ROOT, "Externals/meshoptimizer")

local GAME_APP_ROOT = path.join(GAME_PROJECT_ROOT, GAME_NAME)

local function NEM_AddGameProjectFiles()
    files {
        path.join(GAME_APP_ROOT, "**.h"),
        path.join(GAME_APP_ROOT, "**.hpp"),
        path.join(GAME_APP_ROOT, "**.inl"),
        path.join(GAME_APP_ROOT, "**.cpp"),
        path.join(GAME_APP_ROOT, "**.c"),
        path.join(GAME_APP_ROOT, "**.natvis"),
        path.join(GAME_APP_ROOT, "**.hlsl"),
        path.join(GAME_APP_ROOT, "**.fx"),
        path.join(GAME_APP_ROOT, "GameAssets/**.*"),
    }

    vpaths {
        ["Source/*"] = {
            path.join(GAME_APP_ROOT, "**.h"),
            path.join(GAME_APP_ROOT, "**.hpp"),
            path.join(GAME_APP_ROOT, "**.inl"),
            path.join(GAME_APP_ROOT, "**.cpp"),
            path.join(GAME_APP_ROOT, "**.c"),
            path.join(GAME_APP_ROOT, "**.natvis"),
        },
        ["Shaders/*"] = {
            path.join(GAME_APP_ROOT, "**.hlsl"),
            path.join(GAME_APP_ROOT, "**.fx"),
        },
        ["GameAssets/*"] = {
            path.join(GAME_APP_ROOT, "GameAssets/**.*"),
        },
    }
end

workspace (GAME_NAME)
    location (GAME_PROJECT_ROOT)
    configurations { "Debug", "Develop", "Release" }
    platforms { "x64" }
    startproject (GAME_NAME)

    filter "platforms:x64"
        architecture "x64"
    filter {}

    NEM_ConfigureWorkspaceLayout(GAME_PROJECT_ROOT)

group "Externals"
    include(path.join(NEMENGINE_ROOT, "Premake", "premakeExternals.lua"))
group ""

include(path.join(NEMENGINE_ROOT, "Premake", "premakeNemengine.lua"))

project (GAME_NAME)
    location (GAME_APP_ROOT)
    kind "WindowedApp"

    NEM_ApplyDefaultCppSettings()
    NEM_AddGameProjectFiles()

    includedirs {
        GAME_APP_ROOT,
    }

    NEM_AddEngineRuntimeLinkSettings()
    NEM_ApplyDefaultConfigFilters()
