-- Project/GameProjects/<container>/<app> に取り込んだゲームを、エンジンソース直リンクのアプリとして生成する。
-- Sandboxと同型(WindowedApp + NEMRuntime公開ABI)でビルドする。
-- 取り込み(Tools/Import)で複製したフォルダを毎回スキャンするだけなので、premake5.luaへの登録追記は不要。
--
-- 1段ネストにしている理由: コンテナを実行時の作業ディレクトリにして、直下のappにある
-- .nemprojectを一意に探索するため。

local gameProjectsRoot = path.join(NEM_PROJECT_ROOT, "GameProjects")

if os.isdir(gameProjectsRoot) then

    for _, appRoot in ipairs(os.matchdirs(path.join(gameProjectsRoot, "*/*"))) do

        -- GameAssetsを持つフォルダだけをゲームプロジェクトとして扱う。
        -- .cache等の作業フォルダや空フォルダは無視する。
        local assetRoot = path.join(appRoot, "GameAssets")
        if os.isdir(assetRoot) then

            local projectName = path.getname(appRoot)

            project (projectName)
                location (appRoot)
                kind "WindowedApp"

                NEM_ApplyDefaultCppSettings()
                NEM_AddProjectFiles(appRoot, assetRoot, "GameAssets", true)

                includedirs {
                    appRoot,
                }

                NEM_AddEngineRuntimeLinkSettings()
                NEM_ApplyDefaultConfigFilters()
        end
    end
end
