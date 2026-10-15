#pragma once

//============================================================================
//	include
//============================================================================
#include "BakedLightingTypes.h"

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	BakedLightingStorage namespace
	//	ベイク結果と依存データを一組で保存する
	//============================================================================
	namespace BakedLightingStorage {

		// 保存成功後にAssetの識別子を返す
		AssetID Save(AssetDatabase& database, const std::string& path, const BakedLightingData& data, std::string& diagnostic);
		// ファイル内容だけから結果を復元する
		BakedLightingData Load(const std::filesystem::path& path, const std::filesystem::path& dataPath);
		// GPUへ渡す範囲と数値を検証する
		void Validate(const BakedLightingData& data);
	}
}
