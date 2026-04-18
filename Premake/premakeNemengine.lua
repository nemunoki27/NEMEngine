project "NEMEngine"
    location (path.join(NEMENGINE_ROOT, "Project"))
    kind "StaticLib"

    NEM_ApplyDefaultCppSettings()

    files {
        path.join(NEMENGINE_ROOT, "Project/Engine/**.cpp"),
        path.join(NEMENGINE_ROOT, "Project/Engine/**.h"),
    }

    NEM_AddEngineIncludeSettings()
    NEM_ApplyDefaultConfigFilters()