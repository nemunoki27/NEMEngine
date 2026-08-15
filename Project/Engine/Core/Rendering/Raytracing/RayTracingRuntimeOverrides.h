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

	//============================================================================
	//	RayTracingEffectRuntimeOverride structure
	//	RayTracingProfileへ重ねる実行時専用の変更値
	//============================================================================
	struct RayTracingEffectRuntimeOverride {

		std::optional<bool> enabled{};
		MaterialParameterSet parameters{};
	};

	//============================================================================
	//	RayTracingRuntimeOverrides class
	//	C#と描画パス間でRayTracingProfileの実行時変更値を共有する
	//============================================================================
	class RayTracingRuntimeOverrides final {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RayTracingRuntimeOverrides(const RayTracingRuntimeOverrides&) = delete;
		RayTracingRuntimeOverrides& operator=(const RayTracingRuntimeOverrides&) = delete;
		RayTracingRuntimeOverrides(RayTracingRuntimeOverrides&&) = delete;
		RayTracingRuntimeOverrides& operator=(RayTracingRuntimeOverrides&&) = delete;

		// エフェクトの有効状態を実行時だけ変更する
		bool SetEnabled(std::string_view effectName, bool enabled);
		// エフェクトの公開パラメータを実行時だけ変更する
		bool SetParameter(std::string_view effectName, MaterialParameterID parameterID,
			std::string_view parameterName, const MaterialParameterValue& value);
		// エフェクトの公開パラメータ変更を1件取り除く
		bool ClearParameter(std::string_view effectName, MaterialParameterID parameterID);
		// エフェクト単位で実行時変更を取り除く
		bool ResetEffect(std::string_view effectName);
		// すべての実行時変更を取り除く
		void ResetAll();

		//--------- accessor -----------------------------------------------------

		const RayTracingEffectRuntimeOverride* Find(const std::string& effectName) const;
		static RayTracingRuntimeOverrides& GetInstance();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		RayTracingRuntimeOverrides() = default;
		~RayTracingRuntimeOverrides() = default;

		//--------- variables ----------------------------------------------------

		std::unordered_map<std::string, RayTracingEffectRuntimeOverride> overrides_{};
	};
} // Engine
