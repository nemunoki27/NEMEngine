project "Sandbox"
    location (path.join(NEMENGINE_ROOT, "Project/Sandbox/"))
    kind "WindowedApp"

    NEM_ApplyDefaultCppSettings()

    files {
        path.join(NEMENGINE_ROOT, "Project/Sandbox/**.cpp"),
        path.join(NEMENGINE_ROOT, "Project/Sandbox/**.h"),
    }

    includedirs {
        path.join(NEMENGINE_ROOT, "Project/Sandbox"),
    }

    NEM_AddEngineRuntimeLinkSettings()
    NEM_ApplyDefaultConfigFilters()