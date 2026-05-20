#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/PostProcessParameterLayout.h>

// c++
#include <cstdint>
#include <vector>

namespace Engine {

	// front
	struct MaterialAsset;

	//============================================================================
	//	PostProcessParameterBufferBuilder class
	//	MaterialAsset.parametersをHLSL側のCBV offsetに合わせて詰める補助クラス。
	//============================================================================
	class PostProcessParameterBufferBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PostProcessParameterBufferBuilder() = default;
		~PostProcessParameterBufferBuilder() = default;

		// MaterialAssetの値をPostProcessParameters用のbyte列に変換する
		static std::vector<uint8_t> Build(const MaterialAsset& material, const PostProcessParameterLayout& layout);
	};
} // Engine
