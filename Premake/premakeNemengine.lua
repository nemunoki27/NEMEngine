-- NEMEngine 本体。外部ライブラリを内部に静的同梱したDLLとして配布する
-- 利用側(Sandbox / Game)は公開ヘッダ Public/NEMEngineRuntime.h と import lib だけで使う
project "NEMEngine"
    location (path.join(NEM_PROJECT_ROOT, "Engine"))
    kind "SharedLib"

    NEM_ApplyDefaultCppSettings()
    NEM_AddEngineProjectFiles()
    NEM_AddEngineIncludeSettings()
    NEM_AddEngineDllLinkSettings()
    NEM_ApplyDefaultConfigFilters()

    -- 公開ABIをdllexportする、利用側ではこのマクロ未定義のためdllimportになる
    defines { "NEMENGINE_BUILD_DLL" }
    includedirs { path.join(NEM_PROJECT_ROOT, "Engine/Public") }

    -- 実行フォルダ(Sandbox出力)へDLL/PDBを必ず配置する
    -- エンジン実装のみ変更した場合、import libのexportが変わらず利用側(Sandbox)のリンクがスキップされ、
    -- 利用側postbuildのDLLコピーも走らないため、実行フォルダのNEMEngine.dllが古いまま残ってしまう。
    -- DLL自身のpostbuildにすることで、エンジンを変更した時は必ずリンクし直され最新DLLが配置される。
    local engineBinDir = path.translate(path.join(NEM_OUTPUT_ROOT, "Bin/%{cfg.buildcfg}/NEMEngine"), "\\")
    local sandboxRuntimeDir = path.translate(path.join(NEM_OUTPUT_ROOT, "Output/%{cfg.buildcfg}/Sandbox"), "\\")
    postbuildcommands {
        'if not exist "' .. sandboxRuntimeDir .. '" mkdir "' .. sandboxRuntimeDir .. '"',
        'copy /Y "' .. engineBinDir .. '\\NEMEngine.dll" "' .. sandboxRuntimeDir .. '\\" >nul',
        'if exist "' .. engineBinDir .. '\\NEMEngine.pdb" copy /Y "' .. engineBinDir .. '\\NEMEngine.pdb" "' .. sandboxRuntimeDir .. '\\" >nul',
    }

    filter "files:**.hlsl"
        buildaction "None"
    filter "files:**.fx"
        buildaction "None"
    filter "files:**.hlsli"
        buildaction "None"
    filter {}
