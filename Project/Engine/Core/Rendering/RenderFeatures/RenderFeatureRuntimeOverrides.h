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

	// Profileの保存値へ重ねるPass単位の実行時変更
	struct RenderFeaturePassRuntimeOverride {

		std::optional<bool> enabled{};
		MaterialParameterSet parameters{};
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

		bool SetEnabled(std::string_view passName, bool enabled);
		bool SetParameter(std::string_view passName,
			MaterialParameterID parameterID,
			std::string_view parameterName,
			const MaterialParameterValue& value);
		bool ClearParameter(std::string_view passName,
			MaterialParameterID parameterID);
		bool ResetPass(std::string_view passName);
		void ResetAll();

		const RenderFeaturePassRuntimeOverride* Find(
			std::string_view passName) const;
		static RenderFeatureRuntimeOverrides& GetInstance();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		RenderFeatureRuntimeOverrides() = default;
		~RenderFeatureRuntimeOverrides() = default;

		std::unordered_map<std::string,
			RenderFeaturePassRuntimeOverride> overrides_{};
	};
} // Engine
