#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Lighting/Interface/ILightExtractor.h>

namespace Engine {

	struct DirectionalLightComponent;
	struct PointLightComponent;
	struct SpotLightComponent;

	//============================================================================
	//	BuiltinLightExtractor template
	//	ビルトインライトを抽出する共通クラス
	//============================================================================
	template <typename TComponent>
	class BuiltinLightExtractor final :
		public ILightExtractor {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		BuiltinLightExtractor() = default;
		~BuiltinLightExtractor() override = default;

		void Extract(ECSWorld& world, FrameLightBatch& batch) override;
	};

	// ライト種別ごとの型名は登録側で明示して扱う
	using DirectionalLightExtractor = BuiltinLightExtractor<DirectionalLightComponent>;
	using PointLightExtractor = BuiltinLightExtractor<PointLightComponent>;
	using SpotLightExtractor = BuiltinLightExtractor<SpotLightComponent>;
} // Engine
