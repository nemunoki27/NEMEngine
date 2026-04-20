NEMENGINE_ROOT  = NEMENGINE_ROOT or path.getabsolute(path.join(_SCRIPT_DIR, ".."))
NEM_PROJECT_ROOT = path.join(NEMENGINE_ROOT, "Project")
NEM_GENERATED_ROOT = path.join(NEMENGINE_ROOT, "Generated")

function NEM_ConfigureWorkspaceLayout(runtimeDebugDir)
    objdir(path.join(NEM_GENERATED_ROOT, "Intermediate/%{prj.name}/%{cfg.buildcfg}"))

    filter "kind:StaticLib"
        targetdir(path.join(NEM_GENERATED_ROOT, "Bin/%{cfg.buildcfg}/%{prj.name}"))

    filter "kind:ConsoleApp or kind:WindowedApp"
        targetdir(path.join(NEM_GENERATED_ROOT, "Output/%{cfg.buildcfg}/%{prj.name}"))
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
        path.join(NEM_PROJECT_ROOT, "Externals/assimp/include"),
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

    links {
        "NEMEngine",
        "imgui",
        "DirectXTex",
    }

    linkoptions {
        "/WX",
        "/IGNORE:4099",
    }

    postbuildcommands {
        'copy /Y "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64\\dxcompiler.dll" "$(TargetDir)dxcompiler.dll"',
        'copy /Y "$(WindowsSdkDir)bin\\$(TargetPlatformVersion)\\x64\\dxil.dll" "$(TargetDir)dxil.dll"',
    }

    filter "configurations:Debug"
        libdirs {
            path.join(NEM_PROJECT_ROOT, "Externals/assimp/lib/Debug"),
            path.join(NEM_PROJECT_ROOT, "Externals/meshoptimizer/lib/Debug"),
        }

        links {
            "assimp-vc143-mtd",
            "meshoptimizer",
        }

    filter "configurations:Develop"
        libdirs {
            path.join(NEM_PROJECT_ROOT, "Externals/assimp/lib/Release"),
            path.join(NEM_PROJECT_ROOT, "Externals/meshoptimizer/lib/Release"),
        }

        links {
            "assimp-vc143-mt",
            "meshoptimizer",
        }

    filter "configurations:Release"
        libdirs {
            path.join(NEM_PROJECT_ROOT, "Externals/assimp/lib/Release"),
            path.join(NEM_PROJECT_ROOT, "Externals/meshoptimizer/lib/Release"),
        }

        links {
            "assimp-vc143-mt",
            "meshoptimizer",
        }

    filter {}
end

function NEM_AddEngineProjectFiles()
    files {
        path.join(NEM_PROJECT_ROOT, "Engine/**.h"),
        path.join(NEM_PROJECT_ROOT, "Engine/**.hpp"),
        path.join(NEM_PROJECT_ROOT, "Engine/**.inl"),
        path.join(NEM_PROJECT_ROOT, "Engine/**.cpp"),
        path.join(NEM_PROJECT_ROOT, "Engine/**.c"),
        path.join(NEM_PROJECT_ROOT, "Engine/**.natvis"),

        -- Engine専用アセットを表示したい場合
        path.join(NEM_PROJECT_ROOT, "EngineAssets/**.*"),
    }

    vpaths {
        ["Source/*"] = {
            path.join(NEM_PROJECT_ROOT, "Engine/**.h"),
            path.join(NEM_PROJECT_ROOT, "Engine/**.hpp"),
            path.join(NEM_PROJECT_ROOT, "Engine/**.inl"),
            path.join(NEM_PROJECT_ROOT, "Engine/**.cpp"),
            path.join(NEM_PROJECT_ROOT, "Engine/**.c"),
            path.join(NEM_PROJECT_ROOT, "Engine/**.natvis"),
        },
        ["Assets/*"] = {
            path.join(NEM_PROJECT_ROOT, "EngineAssets/**.*"),
        },
    }
end

function NEM_AddSandboxProjectFiles()
    files {
        path.join(NEM_PROJECT_ROOT, "Sandbox/**.h"),
        path.join(NEM_PROJECT_ROOT, "Sandbox/**.hpp"),
        path.join(NEM_PROJECT_ROOT, "Sandbox/**.inl"),
        path.join(NEM_PROJECT_ROOT, "Sandbox/**.cpp"),
        path.join(NEM_PROJECT_ROOT, "Sandbox/**.c"),
        path.join(NEM_PROJECT_ROOT, "Sandbox/**.natvis"),
        path.join(NEM_PROJECT_ROOT, "Sandbox/**.hlsl"),
        path.join(NEM_PROJECT_ROOT, "Sandbox/**.fx"),

        -- Sandbox専用アセットだけ表示する
        path.join(NEM_PROJECT_ROOT, "Sandbox/GameAssets/**.*"),
    }

    vpaths {
        ["Source/*"] = {
            path.join(NEM_PROJECT_ROOT, "Sandbox/**.h"),
            path.join(NEM_PROJECT_ROOT, "Sandbox/**.hpp"),
            path.join(NEM_PROJECT_ROOT, "Sandbox/**.inl"),
            path.join(NEM_PROJECT_ROOT, "Sandbox/**.cpp"),
            path.join(NEM_PROJECT_ROOT, "Sandbox/**.c"),
            path.join(NEM_PROJECT_ROOT, "Sandbox/**.natvis"),
        },
        ["Shaders/*"] = {
            path.join(NEM_PROJECT_ROOT, "Sandbox/**.hlsl"),
            path.join(NEM_PROJECT_ROOT, "Sandbox/**.fx"),
        },
        ["GameAssets/*"] = {
            path.join(NEM_PROJECT_ROOT, "Sandbox/GameAssets/**.*"),
        },
    }
end

NEMENGINE_ROOT = NEMENGINE_ROOT or path.getabsolute(path.join(_SCRIPT_DIR, ".."))
NEM_GENERATED_ROOT = NEM_GENERATED_ROOT or path.join(NEMENGINE_ROOT, "Generated")

function NEM_ConfigureWorkspaceLayout(runtimeDebugDir)
    objdir(path.join(NEM_GENERATED_ROOT, "Intermediate/%{prj.name}/%{cfg.buildcfg}"))

    filter "kind:StaticLib"
        targetdir(path.join(NEM_GENERATED_ROOT, "Bin/%{cfg.buildcfg}/%{prj.name}"))

    filter "kind:ConsoleApp or kind:WindowedApp"
        targetdir(path.join(NEM_GENERATED_ROOT, "Output/%{cfg.buildcfg}/%{prj.name}"))
        debugdir(runtimeDebugDir)

    filter {}
end