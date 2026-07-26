-- RuntimeとEditorが共有するエンジンCore
project "NEMCore"
    location (path.join(NEM_PROJECT_ROOT, "Engine/Core"))
    kind "StaticLib"

    NEM_ApplyDefaultCppSettings()
    NEM_AddCoreProjectFiles()
    NEM_AddEngineIncludeSettings()
    NEM_ApplyDefaultConfigFilters()

    filter "files:**.hlsl"
        buildaction "None"
    filter "files:**.fx"
        buildaction "None"
    filter "files:**.hlsli"
        buildaction "None"
    filter {}

-- ゲーム向け公開C ABIだけを持つRuntime DLL
project "NEMRuntime"
    location (path.join(NEM_PROJECT_ROOT, "Engine"))
    kind "SharedLib"

    NEM_ApplyDefaultCppSettings()
    NEM_AddRuntimeProjectFiles()
    NEM_AddEngineIncludeSettings()
    NEM_AddEngineDllLinkSettings()
    NEM_ApplyDefaultConfigFilters()
    links { "NEMCore" }
    dependson { "NEMCore" }

    -- 公開ABIをdllexportする、利用側ではこのマクロ未定義のためdllimportになる
    defines { "NEMENGINE_BUILD_DLL" }
    includedirs { path.join(NEM_PROJECT_ROOT, "Engine/Public") }

    filter {}

    -- 実行フォルダ(Sandbox出力)へDLL/PDBを必ず配置する
    -- エンジン実装のみ変更した場合、import libのexportが変わらず利用側(Sandbox)のリンクがスキップされ、
    -- DLL自身のpostbuildにすることで、エンジンを変更した時は必ず最新DLLが配置される。
    local engineBinDir = path.translate(path.join(NEM_OUTPUT_ROOT, "Bin/%{cfg.buildcfg}/NEMRuntime"), "\\")
    local sandboxRuntimeDir = path.translate(path.join(NEM_OUTPUT_ROOT, "Output/%{cfg.buildcfg}/Sandbox"), "\\")
    local runtimeDeployScript = path.translate(
        path.join(NEMENGINE_ROOT, "Tools/DeployRuntimeDependencies.ps1"), "\\")
    postbuildcommands {
        'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. runtimeDeployScript ..
        '" -TargetDirectory "' .. sandboxRuntimeDir .. '" -Configuration "$(Configuration)"' ..
        ' -WindowsSdkBinaryDirectory "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64"' ..
        ' -RuntimeDllPath "' .. engineBinDir .. '\\NEMRuntime.dll"',
        'if exist "' .. engineBinDir .. '\\NEMRuntime.pdb" copy /Y "' .. engineBinDir .. '\\NEMRuntime.pdb" "' .. sandboxRuntimeDir .. '\\" >nul',
    }

    filter "files:**.hlsl"
        buildaction "None"
    filter "files:**.fx"
        buildaction "None"
    filter "files:**.hlsli"
        buildaction "None"
    filter {}

-- Editorは独立した実行ファイルとしてCoreへリンクする
project "NEMEditor"
    location (path.join(NEM_PROJECT_ROOT, "Engine/Editor"))
    kind "WindowedApp"

    NEM_ApplyDefaultCppSettings()
    NEM_AddEditorProjectFiles()
    NEM_AddEngineIncludeSettings()
    NEM_AddEngineDllLinkSettings()
    NEM_ApplyDefaultConfigFilters()
    defines { "NEM_EDITOR_UI_ENABLED" }
    links {
        "NEMCore",
        "imgui",
    }
    dependson { "NEMCore" }
    debugdir (path.join(NEM_PROJECT_ROOT, "Sandbox"))

    filter "configurations:Release"
        symbols "On"

    filter "files:**.hlsl"
        buildaction "None"
    filter "files:**.fx"
        buildaction "None"
    filter "files:**.hlsli"
        buildaction "None"

    filter {}

-- Cookや検証をヘッドレスで実行するCLI
project "NEMBuildTool"
    location (path.join(NEM_PROJECT_ROOT, "Tools/NEM.BuildTool"))
    kind "ConsoleApp"

    NEM_ApplyDefaultCppSettings()
    files {
        path.join(NEM_PROJECT_ROOT, "Tools/NEM.BuildTool/**.h"),
        path.join(NEM_PROJECT_ROOT, "Tools/NEM.BuildTool/**.cpp"),
    }
    NEM_AddEngineIncludeSettings()
    NEM_AddEngineDllLinkSettings()
    NEM_ApplyDefaultConfigFilters()
    links { "NEMCore" }
    dependson { "NEMCore" }

-- Coreの高速な単体・契約テスト
project "NEMTests"
    location (path.join(NEM_PROJECT_ROOT, "Tests"))
    kind "ConsoleApp"

    NEM_ApplyDefaultCppSettings()
    files {
        path.join(NEM_PROJECT_ROOT, "Tests/**.h"),
        path.join(NEM_PROJECT_ROOT, "Tests/**.cpp"),
    }
    NEM_AddEngineIncludeSettings()
    NEM_AddEngineDllLinkSettings()
    NEM_ApplyDefaultConfigFilters()
    links { "NEMCore" }
    dependson { "NEMCore" }
