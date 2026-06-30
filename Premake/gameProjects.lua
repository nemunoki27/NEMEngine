-- Project/GameProjects/<container>/<app> に取り込んだゲームを、エンジンソース直リンクのアプリとして生成する。
-- Sandboxと同型(WindowedApp + NEMEngineソースを直接リンク)でビルドするため、
-- ゲーム実行中にエンジン側へブレークポイントを置いてデバッグできる。
-- 取り込み(Tools/Import)で複製したフォルダを毎回スキャンするだけなので、premake5.luaへの登録追記は不要。
--
-- 1段ネストにしている理由: エンジンの実行時ルート解決(RuntimePaths::FindGameRoot)は、作業ディレクトリ直下に
-- GameAssetsがあると「ゲーム未検出」と誤判定してSandboxへフォールバックする。これを避けるため、各ゲームを
-- 専用コンテナ(<container>)配下に置き、実行時はコンテナをcwdにする(GameAssetsを持つ子はappだけになる)。

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
