#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>

// c++
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Engine {

	struct RenderFeatureProfileAsset;

	// Profileの保存値へ重ねるPass単位の実行時変更
	struct RenderFeaturePassRuntimeOverride {

		std::optional<bool> enabled{};
		MaterialParameterSet parameters{};
		std::unordered_map<std::string, AssetID> textureOverrides{};
	};

	//============================================================================
	//	RenderFeatureRuntimeOverrides class
	//	C#とRenderFeaturePassの間で実行時変更値を共有する
	//============================================================================
	class RenderFeatureRuntimeOverrides final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderFeatureRuntimeOverrides(
			const RenderFeatureRuntimeOverrides&) = delete;
		RenderFeatureRuntimeOverrides& operator=(
			const RenderFeatureRuntimeOverrides&) = delete;

		// 全パス共通のView出力を実行中だけ変更する
		void SetViewEnabled(AssetID profile, bool gameView, bool sceneView);
		bool IsViewEnabled(AssetID profile, bool sceneView, bool savedValue) const;

		bool SetEnabled(UUID passID, bool enabled);
		bool IsEnabled(UUID passID, bool savedValue) const;
		bool SetGroupEnabled(std::string_view groupName, bool enabled);
		bool SetParameter(UUID passID,
			MaterialParameterID parameterID,
			std::string_view parameterName,
			const MaterialParameterValue& value);
		bool ClearParameter(UUID passID,
			MaterialParameterID parameterID);
		bool ResetPass(UUID passID);
		void ResetAll();

		const RenderFeaturePassRuntimeOverride* Find(
			UUID passID) const;
		bool IsGroupEnabled(std::string_view groupName,
			bool fallback) const;
		static RenderFeatureRuntimeOverrides& GetInstance();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		RenderFeatureRuntimeOverrides() = default;
		~RenderFeatureRuntimeOverrides() = default;

		std::unordered_map<AssetID, std::pair<bool, bool>> viewOverrides_{};
		std::unordered_map<UUID,
			RenderFeaturePassRuntimeOverride> overrides_{};
		std::unordered_map<std::string, bool> groupEnabledOverrides_{};
	};
} // Engine
