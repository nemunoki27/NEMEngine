NEMENGINE_ROOT = NEMENGINE_ROOT or path.getabsolute(path.join(_SCRIPT_DIR, ".."))
NEM_PROJECT_ROOT = path.join(NEMENGINE_ROOT, "Project")

-- Engine-owned generated files, such as CMake-generated assimp headers.
NEM_ENGINE_GENERATED_ROOT = NEM_ENGINE_GENERATED_ROOT or path.join(NEMENGINE_ROOT, "Generated")

-- Workspace output location. Game projects may override this after loading this file.
NEM_OUTPUT_ROOT = NEM_OUTPUT_ROOT or NEM_GENERATED_ROOT or NEM_ENGINE_GENERATED_ROOT
NEM_GENERATED_ROOT = NEM_OUTPUT_ROOT

function NEM_ConfigureWorkspaceLayout(runtimeDebugDir)
    objdir(path.join(NEM_OUTPUT_ROOT, "Intermediate/%{prj.name}/%{cfg.buildcfg}"))

    filter "kind:StaticLib"
        targetdir(path.join(NEM_OUTPUT_ROOT, "Bin/%{cfg.buildcfg}/%{prj.name}"))

    filter "kind:ConsoleApp or kind:WindowedApp"
        targetdir(path.join(NEM_OUTPUT_ROOT, "Output/%{cfg.buildcfg}/%{prj.name}"))
        debugdir(runtimeDebugDir)

    filter {}
end

function NEM_ApplyDefaultCppSettings()
    system "windows"
    language "C++"
    cppdialect "C++20"
    staticruntime "On"
    warnings "High"
    multiprocessorcompile "On"
    buildoptions { "/utf-8" }

    -- VS2022を明示したいときだけ固定
    filter "action:vs2022"
        toolset "v143"

    -- vs2026 のときは toolset を固定しない
    -- 使っている Visual Studio / MSVC の既定値に任せる
    filter {}
end

function NEM_ApplyDefaultConfigFilters()
    filter "configurations:Debug"
        symbols "On"
        defines { "_DEVELOPBUILD" }

    filter "configurations:Develop"
        optimize "On"
        defines { "_DEVELOPBUILD" }

    filter "configurations:Release"
        optimize "On"
        defines { "_RELEASE" }
        buildoptions { "/wd4100" }

    filter {}
end

function NEM_AddEngineIncludeSettings()
    includedirs {
        NEM_PROJECT_ROOT,
        path.join(NEM_PROJECT_ROOT, "Engine"),
    }

    -- 外部ライブラリは externalincludedirs 側へ寄せる
    externalincludedirs {
        path.join(NEM_PROJECT_ROOT, "Externals/spdlog"),
        path.join(NEM_PROJECT_ROOT, "Externals/magic_enum"),

        -- assimp: source headers
        path.join(NEM_PROJECT_ROOT, "Externals/assimp/include"),

        -- assimp: generated headers (assimp/config.h, assimp/revision.h など)
        path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/assimp/include"),

        path.join(NEM_PROJECT_ROOT, "Externals/meshoptimizer/include"),
        path.join(NEM_PROJECT_ROOT, "Externals/DirectXTex"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui"),
        path.join(NEM_PROJECT_ROOT, "Externals/nlohmann"),
    }

    defines {
        '_PROFILE="$(Configuration)"',
        "NOMINMAX",
        "IMGUI_DEFINE_MATH_OPERATORS",
    }
end

function NEM_AddEngineRuntimeLinkSettings()
    NEM_AddEngineIncludeSettings()

    local scriptCoreProject = path.join(NEM_PROJECT_ROOT, "Engine/Managed/NEM.ScriptCore/NEM.ScriptCore.csproj")
    local scriptCoreOutput = path.join(NEM_PROJECT_ROOT, "Engine/Library/Managed")

    links {
        "NEMEngine",
        "imgui",
        "DirectXTex",
        "assimp",
        "meshoptimizer",
    }

    linkoptions {
        "/WX",
        "/IGNORE:4099",
    }

	prebuildcommands {
		'set DOTNET_CLI_UI_LANGUAGE=en',
		'dotnet build "' .. scriptCoreProject .. '" -c "$(Configuration)"',
		'if exist "$(ProjectDir)Scripts\\GameScripts.csproj" dotnet build "$(ProjectDir)Scripts\\GameScripts.csproj" -c "$(Configuration)" --no-dependencies',
	}

    postbuildcommands {
        'copy /Y "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64\\dxcompiler.dll" "$(TargetDir)dxcompiler.dll"',
        'copy /Y "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64\\dxil.dll" "$(TargetDir)dxil.dll"',
        'if exist "' .. scriptCoreOutput .. '\\$(Configuration)\\*" xcopy /Y /I "' .. scriptCoreOutput .. '\\$(Configuration)\\*" "$(TargetDir)Managed\\"',
        'if exist "$(ProjectDir)Managed\\$(Configuration)\\*" xcopy /Y /I "$(ProjectDir)Managed\\$(Configuration)\\*" "$(TargetDir)Managed\\"',
    }

    filter {}
end

function NEM_MakeProjectSourcePatterns(projectRoot)
    return {
        path.join(projectRoot, "**.h"),
        path.join(projectRoot, "**.hpp"),
        path.join(projectRoot, "**.inl"),
        path.join(projectRoot, "**.cpp"),
        path.join(projectRoot, "**.c"),
        path.join(projectRoot, "**.cs"),
        path.join(projectRoot, "**.csproj"),
        path.join(projectRoot, "**.natvis"),
    }
end

function NEM_MakeProjectShaderPatterns(projectRoot)
    return {
        path.join(projectRoot, "**.hlsl"),
        path.join(projectRoot, "**.fx"),
    }
end

function NEM_AppendPatterns(target, source)
    for _, value in ipairs(source) do
        table.insert(target, value)
    end
end

function NEM_AddProjectFiles(projectRoot, assetRoot, assetVpathName, includeShaders)
    local sourcePatterns = NEM_MakeProjectSourcePatterns(projectRoot)
    local filePatterns = {}
    NEM_AppendPatterns(filePatterns, sourcePatterns)

    local shaderPatterns = {}
    if includeShaders then
        shaderPatterns = NEM_MakeProjectShaderPatterns(projectRoot)
        NEM_AppendPatterns(filePatterns, shaderPatterns)
    end

    table.insert(filePatterns, path.join(assetRoot, "**.*"))
    files(filePatterns)

    local projectVpaths = {}
    projectVpaths["Source/*"] = sourcePatterns
    if includeShaders then
        projectVpaths["Shaders/*"] = shaderPatterns
    end
    projectVpaths[assetVpathName .. "/*"] = {
        path.join(assetRoot, "**.*"),
    }
    vpaths(projectVpaths)
end

function NEM_AddEngineProjectFiles()
    -- Engine専用アセットを表示したい場合
    NEM_AddProjectFiles(path.join(NEM_PROJECT_ROOT, "Engine"),
        path.join(NEM_PROJECT_ROOT, "EngineAssets"), "Assets", false)
end

function NEM_AddSandboxProjectFiles()
    -- Sandbox専用アセットだけ表示する
    NEM_AddProjectFiles(path.join(NEM_PROJECT_ROOT, "Sandbox"),
        path.join(NEM_PROJECT_ROOT, "Sandbox/GameAssets"), "GameAssets", true)
end
