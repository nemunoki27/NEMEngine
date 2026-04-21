if NEM_USE_EXTERNAL_ENGINE_PROJECT then
    externalproject "NEMEngine"
        location (path.join(NEM_PROJECT_ROOT, "Engine"))
        filename "NEMEngine"
        kind "StaticLib"
        language "C++"
else
    project "NEMEngine"
        location (path.join(NEM_PROJECT_ROOT, "Engine"))
        kind "StaticLib"

        NEM_ApplyDefaultCppSettings()
        NEM_AddEngineProjectFiles()
        NEM_AddEngineIncludeSettings()
        NEM_ApplyDefaultConfigFilters()
end
