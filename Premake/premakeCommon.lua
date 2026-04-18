NEMENGINE_ROOT = NEMENGINE_ROOT or path.getabsolute(path.join(_SCRIPT_DIR, ".."))

function NEM_ApplyDefaultCppSettings()
    system "windows"
    language "C++"
    cppdialect "C++20"
    toolset "v143"
    staticruntime "On"
    warnings "High"
    multiprocessorcompile "On"
    buildoptions { "/utf-8" }
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
        path.join(NEMENGINE_ROOT, "Project"),
        path.join(NEMENGINE_ROOT, "Project/Engine"),
        path.join(NEMENGINE_ROOT, "Project/Externals/spdlog"),
        path.join(NEMENGINE_ROOT, "Project/Externals/magic_enum"),
        path.join(NEMENGINE_ROOT, "Project/Externals/assimp/include"),
        path.join(NEMENGINE_ROOT, "Project/Externals/meshoptimizer/include"),
        path.join(NEMENGINE_ROOT, "Project/Externals/DirectXTex"),
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui"),
        path.join(NEMENGINE_ROOT, "Project/Externals/nlohmann"),
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
            path.join(NEMENGINE_ROOT, "Project/Externals/assimp/lib/Debug"),
            path.join(NEMENGINE_ROOT, "Project/Externals/meshoptimizer/lib/Debug"),
        }

        links {
            "assimp-vc143-mtd",
            "meshoptimizer",
        }

    filter "configurations:Develop"
        libdirs {
            path.join(NEMENGINE_ROOT, "Project/Externals/assimp/lib/Release"),
            path.join(NEMENGINE_ROOT, "Project/Externals/meshoptimizer/lib/Release"),
        }

        links {
            "assimp-vc143-mt",
            "meshoptimizer",
        }

    filter "configurations:Release"
        libdirs {
            path.join(NEMENGINE_ROOT, "Project/Externals/assimp/lib/Release"),
            path.join(NEMENGINE_ROOT, "Project/Externals/meshoptimizer/lib/Release"),
        }

        links {
            "assimp-vc143-mt",
            "meshoptimizer",
        }

    filter {}
end