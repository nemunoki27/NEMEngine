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

		static bool Load(const std::filesystem::path& path,
			RenderFeatureProfileAsset& outProfile);
		static bool Save(const std::filesystem::path& path,
			const RenderFeatureProfileAsset& profile);
		static RenderFeatureProfileAsset FromJson(
			const nlohmann::json& data);
		static nlohmann::json ToJson(
			const RenderFeatureProfileAsset& profile);
	};

	bool FromJson(const nlohmann::json& data,
		RenderFeatureProfileAsset& outProfile);
	nlohmann::json ToJson(const RenderFeatureProfileAsset& profile);
} // Engine
