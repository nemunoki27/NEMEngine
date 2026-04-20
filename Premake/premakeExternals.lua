local directXTexProjectName = "DirectXTex_Desktop_2022_Win10"

if os.isfile(path.join(NEM_PROJECT_ROOT, "Externals/DirectXTex/DirectXTex_Desktop_2026_Win10.vcxproj")) then
    directXTexProjectName = "DirectXTex_Desktop_2026_Win10"
end

externalproject "DirectXTex"
    location (path.join(NEM_PROJECT_ROOT, "Externals/DirectXTex"))
    filename (directXTexProjectName)
    kind "StaticLib"
    language "C++"

    configmap {
        ["Develop"] = "Release",
    }

project "imgui"
    location (path.join(NEM_PROJECT_ROOT, "Externals/imgui"))
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
        path.join(NEM_PROJECT_ROOT, "Externals/imgui/*.cpp"),
        path.join(NEM_PROJECT_ROOT, "Externals/imgui/*.h"),
    }

    includedirs {
        path.join(NEM_PROJECT_ROOT, "Externals/imgui"),
    }

    filter "configurations:Debug"
        symbols "On"

    filter "configurations:Release"
        optimize "On"

    filter {}