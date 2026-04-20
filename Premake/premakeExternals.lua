local ASSIMP_BUILD_DIR = path.join(NEMENGINE_ROOT, "Generated/Externals/assimp")

externalproject "DirectXTex"
    location (path.join(NEMENGINE_ROOT, "Project/Externals/DirectXTex"))
    filename "DirectXTex_Desktop_2022_Win10"
    kind "StaticLib"
    language "C++"

    configmap {
        ["Develop"] = "Release",
    }

project "imgui"
    location (path.join(NEMENGINE_ROOT, "Project/Externals/imgui"))
    kind "StaticLib"

    removeconfigurations { "Develop" }
    configmap { ["Develop"] = "Release" }

    system "windows"
    language "C++"
    cppdialect "C++20"
    toolset "v143"
    staticruntime "On"
    warnings "Default"
    multiprocessorcompile "On"
    buildoptions { "/utf-8" }

    files {
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui/*.cpp"),
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui/*.h"),
    }

    includedirs {
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui"),
    }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter {}

externalproject "assimp"
    location (path.join(ASSIMP_BUILD_DIR, "code"))
    filename "assimp"
    kind "StaticLib"
    language "C++"

    configmap {
        ["Develop"] = "Release",
    }