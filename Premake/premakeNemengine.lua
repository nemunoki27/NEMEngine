-- NEMRuntime本体。外部ライブラリを内部に静的同梱したDLLとして配布する
-- 利用側(Sandbox / Game)は公開ヘッダ Public/NEMEngineRuntime.h と import lib だけで使う
project "NEMRuntime"
    location (path.join(NEM_PROJECT_ROOT, "Engine"))
    kind "SharedLib"

    NEM_ApplyDefaultCppSettings()
    NEM_AddRuntimeProjectFiles()
    NEM_AddEngineIncludeSettings()
    NEM_AddEngineDllLinkSettings()
    NEM_ApplyDefaultConfigFilters()

    -- 公開ABIをdllexportする、利用側ではこのマクロ未定義のためdllimportになる
    defines { "NEMENGINE_BUILD_DLL" }
    includedirs { path.join(NEM_PROJECT_ROOT, "Engine/Public") }

    filter "configurations:Debug or Develop"
        defines { "NEM_EDITOR_UI_ENABLED" }

    filter "configurations:Release"
        removefiles {
            path.join(NEM_PROJECT_ROOT, "Engine/Core/Tools/ImGui/**"),
            path.join(NEM_PROJECT_ROOT, "Engine/Core/Rendering/Particle/Gui/**"),
        }

    filter {}

    -- 実行フォルダ(Sandbox出力)へDLL/PDBを必ず配置する
    -- エンジン実装のみ変更した場合、import libのexportが変わらず利用側(Sandbox)のリンクがスキップされ、
    -- DLL自身のpostbuildにすることで、エンジンを変更した時は必ず最新DLLが配置される。
    local engineBinDir = path.translate(path.join(NEM_OUTPUT_ROOT, "Bin/%{cfg.buildcfg}/NEMRuntime"), "\\")
    local sandboxRuntimeDir = path.translate(path.join(NEM_OUTPUT_ROOT, "Output/%{cfg.buildcfg}/Sandbox"), "\\")
    postbuildcommands {
        'if not exist "' .. sandboxRuntimeDir .. '" mkdir "' .. sandboxRuntimeDir .. '"',
        'copy /Y "' .. engineBinDir .. '\\NEMRuntime.dll" "' .. sandboxRuntimeDir .. '\\" >nul',
        'if exist "' .. engineBinDir .. '\\NEMRuntime.pdb" copy /Y "' .. engineBinDir .. '\\NEMRuntime.pdb" "' .. sandboxRuntimeDir .. '\\" >nul',
    }

    filter "files:**.hlsl"
        buildaction "None"
    filter "files:**.fx"
        buildaction "None"
    filter "files:**.hlsli"
        buildaction "None"
    filter {}

-- Editor実装はDebug/DevelopだけNEMRuntimeへ静的リンクする
project "NEMEditor"
    location (path.join(NEM_PROJECT_ROOT, "Engine"))
    kind "StaticLib"

    NEM_ApplyDefaultCppSettings()
    NEM_AddEditorProjectFiles()
    NEM_AddEngineIncludeSettings()
    NEM_ApplyDefaultConfigFilters()

    filter "configurations:Release"
        removefiles {
            path.join(NEM_PROJECT_ROOT, "Engine/Editor/**"),
            path.join(NEM_PROJECT_ROOT, "Engine/Assets/Shaders/Builtin/Editor/**"),
            path.join(NEM_PROJECT_ROOT, "Engine/Assets/Textures/Editor/**"),
        }

    filter "files:**.hlsl"
        buildaction "None"
    filter "files:**.fx"
        buildaction "None"
    filter "files:**.hlsli"
        buildaction "None"

    filter {}
