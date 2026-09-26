NEMENGINE_ROOT = NEMENGINE_ROOT or path.getabsolute(path.join(_SCRIPT_DIR, ".."))
NEM_PROJECT_ROOT = path.join(NEMENGINE_ROOT, "Project")

-- Engine-owned generated files, such as CMake-generated assimp headers.
NEM_ENGINE_GENERATED_ROOT = NEM_ENGINE_GENERATED_ROOT or path.join(NEMENGINE_ROOT, "Generated")

-- Workspace output location. Game projects may override this after loading this file.
NEM_OUTPUT_ROOT = NEM_OUTPUT_ROOT or NEM_GENERATED_ROOT or NEM_ENGINE_GENERATED_ROOT
NEM_GENERATED_ROOT = NEM_OUTPUT_ROOT

-- msdf-atlas-gen のsubmoduleが取得済みかどうか、未取得でもエンジンビルドが通るよう分岐に使う
NEM_MSDF_AVAILABLE = os.isfile(path.join(NEM_PROJECT_ROOT, "Externals/msdf-atlas-gen/CMakeLists.txt"))

function NEM_ConfigureWorkspaceLayout(runtimeDebugDir)
    objdir(path.join(NEM_OUTPUT_ROOT, "Intermediate/%{prj.name}/%{cfg.buildcfg}"))

    filter "kind:StaticLib or kind:SharedLib"
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
    buildoptions { "/utf-8", "/FS" }

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
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor"),
        path.join(NEM_PROJECT_ROOT, "Externals/nlohmann"),

        -- WinPixEventRuntime: <WinPixEventRuntime/pix3.h>
        path.join(NEM_PROJECT_ROOT, "Externals/WinPixEventRuntime/Include"),

        -- .NET native hosting: <nethost.h> / <hostfxr.h> / <coreclr_delegates.h>
        path.join(NEM_PROJECT_ROOT, "Externals/dotnet-hosting/include"),
    }

    -- msdf-atlas-gen: <msdf-atlas-gen/msdf-atlas-gen.h> と内部の <msdfgen.h> / <core/...>
    if NEM_MSDF_AVAILABLE then
        externalincludedirs {
            path.join(NEM_PROJECT_ROOT, "Externals/msdf-atlas-gen"),
            path.join(NEM_PROJECT_ROOT, "Externals/msdf-atlas-gen/msdfgen"),
        }
    end

    defines {
        '_PROFILE="$(Configuration)"',
        "NOMINMAX",
        "IMGUI_DEFINE_MATH_OPERATORS",
    }

    -- msdf-atlas-gen は静的リンクなので公開マクロを空定義し、ライブラリと同じC++11設定に揃える
    if NEM_MSDF_AVAILABLE then
        defines {
            "NEM_USE_MSDF_ATLAS_GEN",
            "MSDFGEN_PUBLIC=",
            "MSDFGEN_EXT_PUBLIC=",
            "MSDF_ATLAS_PUBLIC=",
            "MSDFGEN_USE_CPP11",
        }
    end
end

-- エンジンDLL自身が同梱する外部ライブラリとシステムライブラリをリンクする
-- 外部ライブラリはDLL内部に静的に取り込み、利用側からは見えなくする
function NEM_AddEngineDllLinkSettings()
    links {
        "meshoptimizer",
        "bcrypt",
    }
    dependson {
        "DirectXTex",
        "assimp",
    }

    -- msdf-atlas-gen 一式、依存順にmsdf-atlas-gen -> msdfgen -> freetypeで取り込む
    if NEM_MSDF_AVAILABLE then
        dependson {
            "msdf-atlas-gen",
            "msdfgen-ext",
            "msdfgen-core",
            "freetype",
        }
    end

    filter "configurations:Debug"
        links {
            path.join(NEM_ENGINE_GENERATED_ROOT, "Outputs/DirectXTex/Debug/DirectXTex.lib"),
            path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/assimp/lib/Debug/assimp-vc145-mtd.lib"),
            path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/assimp/contrib/zlib/Debug/zlibstaticd.lib"),
        }
        if NEM_MSDF_AVAILABLE then
            links {
                path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/msdf/msdf-atlas-gen/Debug/msdf-atlas-gen.lib"),
                path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/msdf/msdf-atlas-gen/msdfgen/Debug/msdfgen-ext.lib"),
                path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/msdf/msdf-atlas-gen/msdfgen/Debug/msdfgen-core.lib"),
                path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/msdf/freetype/Debug/freetyped.lib"),
            }
        end

    filter "configurations:Develop or Release"
        links {
            path.join(NEM_ENGINE_GENERATED_ROOT, "Outputs/DirectXTex/Release/DirectXTex.lib"),
            path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/assimp/lib/Release/assimp-vc145-mt.lib"),
            path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/assimp/contrib/zlib/Release/zlibstatic.lib"),
        }
        if NEM_MSDF_AVAILABLE then
            links {
                path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/msdf/msdf-atlas-gen/Release/msdf-atlas-gen.lib"),
                path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/msdf/msdf-atlas-gen/msdfgen/Release/msdfgen-ext.lib"),
                path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/msdf/msdf-atlas-gen/msdfgen/Release/msdfgen-core.lib"),
                path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/msdf/freetype/Release/freetype.lib"),
            }
        end

    filter {}
    linkoptions {
        "/WX",
        "/IGNORE:4099",
    }

    -- WinPixEventRuntime。Debugではpix3.hが_DEBUG経由でUSE_PIXを有効化しPIXシンボルを参照するため
    -- import libをリンクする。DLL本体の配置はアプリ側postbuildで実行ファイル横へコピーする
    filter "configurations:Debug"
        libdirs { path.translate(path.join(NEM_PROJECT_ROOT, "Externals/WinPixEventRuntime/bin/x64"), "\\") }
        links { "WinPixEventRuntime" }

    -- Coreへ静的リンクする全EXEへ共通ランタイム依存を配置する
    -- 依存一覧と必須ファイル検証はDeployRuntimeDependencies.ps1へ集約する
    local runtimeDeployScript = path.translate(
        path.join(NEMENGINE_ROOT, "Tools/DeployRuntimeDependencies.ps1"), "\\")
    filter "kind:ConsoleApp or kind:WindowedApp"
        postbuildcommands {
            'powershell -NoProfile -ExecutionPolicy Bypass -File "' .. runtimeDeployScript ..
            '" -TargetPath "$(TargetPath)" -Configuration "$(Configuration)"' ..
            ' -WindowsSdkBinaryDirectory "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64"',
        }

    filter {}
end

-- アプリ(Sandbox / Game)側のエンジン参照設定
-- 公開ヘッダとNEMRuntimeのimport libだけに依存させ、エンジンソースや外部ライブラリには触れさせない
-- managedビルドと実行時DLL配置(NEMRuntime.dll / dxc / nethost / Managed等)は
-- patch_vcxproj_managed_config.ps1 のPre/PostBuildEventで一元管理する
function NEM_AddEngineRuntimeLinkSettings()
    includedirs {
        path.join(NEM_PROJECT_ROOT, "Engine/Public"),
    }

    links {
        "NEMRuntime",
    }

    linkoptions {
        "/WX",
        "/IGNORE:4099",
    }

    -- managedビルドと実行時DLL配置の本体は patch_vcxproj_managed_config.ps1 が上書きする
    -- パッチが書き込む先のPre/PostBuildEventを生成しておくためのプレースホルダ
    prebuildcommands {
        'set DOTNET_CLI_UI_LANGUAGE=en',
    }
    postbuildcommands {
        'rem NEMRuntime deployment is configured by patch_vcxproj_managed_config.ps1',
    }
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
        path.join(projectRoot, "**.props"),
        path.join(projectRoot, "**.targets"),
        path.join(projectRoot, "**.natvis"),
    }
end

function NEM_MakeProjectShaderPatterns(projectRoot)
    return {
        path.join(projectRoot, "**.hlsl"),
        path.join(projectRoot, "**.fx"),
        path.join(projectRoot, "**.hlsli"),
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

    if includeShaders then
        -- HLSLはエンジン内のDXC実行時コンパイルで扱う
        -- Visual Studio/MSBuildのFxCompileに渡すと既定のvs_2_0などで誤コンパイルされるため
        -- ソリューション表示用のNone項目として登録する
        filter "files:**.hlsl"
            buildaction "None"
        filter "files:**.fx"
            buildaction "None"
        filter "files:**.hlsli"
            buildaction "None"
        filter {}
    end

    removefiles {
        path.join(projectRoot, "**/bin/**"),
        path.join(projectRoot, "**/obj/**"),
        path.join(projectRoot, "Library/**"),
        -- Edit reload の作業領域（staging / shadow copy / last-known-good）はプロジェクトへ含めない
        path.join(projectRoot, "Managed/Staging/**"),
        path.join(projectRoot, "Managed/Shadow/**"),
        path.join(projectRoot, "Managed/LastKnownGood/**"),
        -- コードスタイルの見本ファイルはビルド対象に含めない（意図的に不正なC++を含むため）
        path.join(projectRoot, "templateClass.*"),
        path.join(projectRoot, "**/templateClass.*"),
    }
end

function NEM_AddCoreProjectFiles()
    local coreRoot = path.join(NEM_PROJECT_ROOT, "Engine/Core")
    local sourcePatterns = NEM_MakeProjectSourcePatterns(coreRoot)
    files(sourcePatterns)
    vpaths {
        ["Source/*"] = sourcePatterns,
    }

    removefiles {
        path.join(coreRoot, "Tools/ImGui/**"),
    }
end

function NEM_AddRuntimeProjectFiles()
    local publicRoot = path.join(NEM_PROJECT_ROOT, "Engine/Public")
    local sourcePatterns = NEM_MakeProjectSourcePatterns(publicRoot)
    files(sourcePatterns)
    vpaths {
        ["Public/*"] = sourcePatterns,
    }
end

function NEM_AddEditorProjectFiles()
    local editorRoot = path.join(NEM_PROJECT_ROOT, "Engine/Editor")
    local sourcePatterns = NEM_MakeProjectSourcePatterns(editorRoot)
    files(sourcePatterns)
    vpaths {
        ["Source/*"] = sourcePatterns,
    }

    local editorAssets = {
        path.join(NEM_PROJECT_ROOT, "Engine/Assets/Shaders/Builtin/Editor/**.*"),
        path.join(NEM_PROJECT_ROOT, "Engine/Assets/Textures/Editor/**.*"),
    }
    files(editorAssets)
    vpaths {
        ["EditorAssets/*"] = editorAssets,
    }

    -- ImGuiの実装はEditorだけで構築する
    local editorOwnedCore = {
        path.join(NEM_PROJECT_ROOT, "Engine/Core/Tools/ImGui/**.h"),
        path.join(NEM_PROJECT_ROOT, "Engine/Core/Tools/ImGui/**.cpp"),
    }
    files(editorOwnedCore)
    vpaths {
        ["EditorOwnedCore/*"] = editorOwnedCore,
    }
end

function NEM_AddSandboxProjectFiles()
    -- Sandbox専用アセットだけ表示する
    NEM_AddProjectFiles(path.join(NEM_PROJECT_ROOT, "Sandbox"),
        path.join(NEM_PROJECT_ROOT, "Sandbox/GameAssets"), "GameAssets", true)
end
