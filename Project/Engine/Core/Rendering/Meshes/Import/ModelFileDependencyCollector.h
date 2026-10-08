#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine::ModelFileDependencyCollector {

	// 通常読込で使うファイルとMaterialごとの画像
	struct ModelDependencies {

		std::vector<std::filesystem::path> files;
		std::vector<std::vector<std::filesystem::path>> materialTextures;

		// 同じ実ファイルを重複させず依存一覧へ追加する
		void AddFile(const std::filesystem::path& path);
	};

	// 一度のモデル読込で依存ファイルと画像解決を揃える
	bool Collect(const std::filesystem::path& modelPath, ModelDependencies& dependencies, std::string& diagnostic);
	// モデル読込で使用する文書・Buffer・画像を収集する
	bool Collect(
		const std::filesystem::path& modelPath, std::vector<std::filesystem::path>& dependencies, std::string& diagnostic);
	// Materialごとの画像解決結果を通常描画と同じ順序で取得する
	bool CollectMaterialTextures(const std::filesystem::path& modelPath,
		std::vector<std::vector<std::filesystem::path>>& textures, std::string& diagnostic);
}
