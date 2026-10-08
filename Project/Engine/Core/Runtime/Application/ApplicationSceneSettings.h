#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetMetadata.h>

// c++
#include <string>

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	ApplicationSceneSettings class
	//	実行形態ごとの起動シーン設定を読み書きする
	//============================================================================
	class ApplicationSceneSettings {
	public:

		// 共有設定と最後に開いたシーンを順に解決する
		static void LoadEditor(AssetDatabase& database, AssetID& activeScene, std::string& activeScenePath);
		// 製品設定を優先して起動シーンを解決する
		static void LoadGame(AssetDatabase& database, AssetID& activeScene);
		// 製品実行以外の最後に開いたシーンを保存する
		static void Save(AssetID activeScene, bool product);
	};
}
