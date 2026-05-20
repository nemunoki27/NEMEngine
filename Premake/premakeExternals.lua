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
    location (NEM_IMGUI_PROJECT_LOCATION or path.join(NEMENGINE_ROOT, "Project/Externals/imgui"))
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

project "imgui_node_editor"
    location (NEM_IMGUI_NODE_EDITOR_PROJECT_LOCATION or path.join(NEM_ENGINE_GENERATED_ROOT, "Projects/imgui-node-editor"))
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
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/crude_json.cpp"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/crude_json.h"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_canvas.cpp"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_canvas.h"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_node_editor_api.cpp"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_node_editor.cpp"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_node_editor.h"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_node_editor_internal.h"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_node_editor_internal.inl"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_bezier_math.h"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_bezier_math.inl"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_extra_math.h"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/imgui_extra_math.inl"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/misc/imgui_node_editor.natvis"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor/misc/crude_json.natvis"),
    }

    includedirs {
        path.join(NEM_PROJECT_ROOT, "Externals/imgui-node-editor"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui"),
    }

    defines {
        "NOMINMAX",
        "IMGUI_DEFINE_MATH_OPERATORS",
    }

    links {
        "imgui",
    }

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
