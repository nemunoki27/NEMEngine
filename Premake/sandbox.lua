project "Sandbox"
    location (path.join(NEM_PROJECT_ROOT, "Sandbox"))
    kind "WindowedApp"

    NEM_ApplyDefaultCppSettings()
    NEM_AddSandboxProjectFiles()

    includedirs {
        path.join(NEM_PROJECT_ROOT, "Sandbox"),
    }

    NEM_AddEngineRuntimeLinkSettings()
    NEM_ApplyDefaultConfigFilters()