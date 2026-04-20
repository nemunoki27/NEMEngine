project "NEMEngine"
    location (path.join(NEM_PROJECT_ROOT, "Engine"))
    kind "StaticLib"

    NEM_ApplyDefaultCppSettings()
    NEM_AddEngineProjectFiles()
    NEM_AddEngineIncludeSettings()
    NEM_ApplyDefaultConfigFilters()