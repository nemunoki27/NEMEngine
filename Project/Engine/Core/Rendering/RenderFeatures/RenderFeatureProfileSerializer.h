#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>

// c++
#include <filesystem>

namespace Engine {

	//============================================================================
	//	RenderFeatureProfileSerializer class
	//	RenderFeatureProfileのJSON変換を担当するクラス
	//============================================================================
	class RenderFeatureProfileSerializer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderFeatureProfileSerializer() = delete;
		~RenderFeatureProfileSerializer() = delete;

		// 保存ファイルを読み込む
		static bool Load(const std::filesystem::path& path, RenderFeatureProfileAsset& outProfile);
		// 保存結果を呼出し元へ返す
		static bool Save(const std::filesystem::path& path, const RenderFeatureProfileAsset& profile);
		// 設定値をJSONから復元する
		static RenderFeatureProfileAsset FromJson(const nlohmann::json& data);
		// 設定値を保存用のJSONへ変換する
		static nlohmann::json ToJson(const RenderFeatureProfileAsset& profile);
	};

	bool FromJson(const nlohmann::json& data, RenderFeatureProfileAsset& outProfile);
	nlohmann::json ToJson(const RenderFeatureProfileAsset& profile);
} // Engine
