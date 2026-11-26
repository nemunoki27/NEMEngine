local ASSIMP_BUILD_DIR = path.join(NEMENGINE_ROOT, "Generated/Externals/assimp")

externalproject "DirectXTex"
    location (path.join(NEMENGINE_ROOT, "Project/Externals/DirectXTex"))
    filename "DirectXTex_Desktop_2022_Win10"
    kind "StaticLib"
    language "C++"

    configmap {
        ["Develop"] = "Release",
    }

local exr = path.join(NEM_PROJECT_ROOT, "Externals/OpenEXRCore")
project "OpenEXRCore"
    location (exr)
    kind "StaticLib"
    system "windows"
    language "C"
    cdialect "C17"
    staticruntime "On"
    warnings "Default"
    multiprocessorcompile "On"
    defines { "_CRT_SECURE_NO_WARNINGS", "NOMINMAX" }
    buildoptions { "/utf-8" }

    removeconfigurations { "Develop" }
    configmap { ["Develop"] = "Release" }

    includedirs {
        path.join(exr, "core"), path.join(exr, "Imath"), path.join(exr, "libdeflate"),
        path.join(NEM_ENGINE_GENERATED_ROOT, "Externals/OpenEXRCore/include"),
    }
    files {
        path.join(exr, "core/*.c"), path.join(exr, "core/*.h"),
        path.join(exr, "libdeflate/lib/x86/cpu_features.c"),
        path.join(exr, "libdeflate/lib/utils.c"), path.join(exr, "libdeflate/lib/adler32.c"),
        path.join(exr, "libdeflate/lib/deflate_*.c"), path.join(exr, "libdeflate/lib/zlib_*.c"),
    }

    filter "configurations:Debug"
        symbols "On"
    filter "configurations:Release"
        optimize "On"
    filter {}

project "imgui"
    location (NEM_IMGUI_PROJECT_LOCATION or path.join(NEMENGINE_ROOT, "Project/Externals/imgui"))
    kind "StaticLib"

    removeconfigurations { "Develop" }
    configmap { ["Develop"] = "Release" }

    system "windows"
    language "C++"
    cppdialect "C++20"
    staticruntime "On"
    warnings "Default"
    multiprocessorcompile "On"
    buildoptions { "/utf-8" }

    filter "action:vs2022"
        toolset "v143"
    filter {}

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

project "imgui-node-editor"
    location (path.join(NEMENGINE_ROOT, "Project/Externals/imgui-node-editor"))
    kind "StaticLib"

    removeconfigurations { "Develop" }
    configmap { ["Develop"] = "Release" }

    system "windows"
    language "C++"
    cppdialect "C++20"
    staticruntime "On"
    warnings "Default"
    multiprocessorcompile "On"
    buildoptions { "/utf-8" }

    files {
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui-node-editor/crude_json.cpp"),
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui-node-editor/imgui_canvas.cpp"),
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui-node-editor/imgui_node_editor.cpp"),
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui-node-editor/imgui_node_editor_api.cpp"),
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui-node-editor/*.h"),
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui-node-editor/*.inl"),
    }

    includedirs {
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui"),
        path.join(NEMENGINE_ROOT, "Project/Externals/imgui-node-editor"),
    }
    dependson { "imgui" }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter {}

project "meshoptimizer"
    location (NEM_MESHOPTIMIZER_PROJECT_LOCATION or path.join(NEMENGINE_ROOT, "Project/Externals/meshoptimizer"))
    kind "StaticLib"

    removeconfigurations { "Develop" }
    configmap { ["Develop"] = "Release" }

    system "windows"
    language "C++"
    cppdialect "C++20"
    staticruntime "On"
    warnings "Default"
    multiprocessorcompile "On"
    buildoptions { "/utf-8" }

    files {
        path.join(NEMENGINE_ROOT, "Project/Externals/meshoptimizer/include/*.cpp"),
        path.join(NEMENGINE_ROOT, "Project/Externals/meshoptimizer/include/*.h"),
    }

    includedirs {
        path.join(NEMENGINE_ROOT, "Project/Externals/meshoptimizer/include"),
    }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter {}

externalproject "zlibstatic"
    location (path.join(ASSIMP_BUILD_DIR, "contrib/zlib"))
    filename "zlibstatic"
    kind "StaticLib"
    language "C++"

    configmap {
        ["Develop"] = "Release",
    }

externalproject "assimp"
    location (path.join(ASSIMP_BUILD_DIR, "code"))
    filename "assimp"
    kind "StaticLib"
    language "C++"

    dependson { "zlibstatic" }

    configmap {
        ["Develop"] = "Release",
    }

-- msdf-atlas-gen はsubmodule未取得でも生成が止まらないよう、ソースがある時だけプロジェクトを宣言する
local MSDF_BUILD_DIR = path.join(NEMENGINE_ROOT, "Generated/Externals/msdf")
if os.isfile(path.join(NEMENGINE_ROOT, "Project/Externals/msdf-atlas-gen/CMakeLists.txt")) then

externalproject "freetype"
    location (path.join(MSDF_BUILD_DIR, "freetype"))
    filename "freetype"
    kind "StaticLib"
    language "C"

    configmap {
        ["Develop"] = "Release",
    }

externalproject "msdfgen-core"
    location (path.join(MSDF_BUILD_DIR, "msdf-atlas-gen/msdfgen"))
    filename "msdfgen-core"
    kind "StaticLib"
    language "C++"

    configmap {
        ["Develop"] = "Release",
    }

externalproject "msdfgen-ext"
    location (path.join(MSDF_BUILD_DIR, "msdf-atlas-gen/msdfgen"))
    filename "msdfgen-ext"
    kind "StaticLib"
    language "C++"

    dependson { "freetype", "msdfgen-core" }

    configmap {
        ["Develop"] = "Release",
    }

externalproject "msdf-atlas-gen"
    location (path.join(MSDF_BUILD_DIR, "msdf-atlas-gen"))
    filename "msdf-atlas-gen"
    kind "StaticLib"
    language "C++"

    dependson { "msdfgen-core", "msdfgen-ext" }

    configmap {
        ["Develop"] = "Release",
    }

end
