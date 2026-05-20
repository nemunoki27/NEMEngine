#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Graph/GraphID.h>

namespace Engine {

	//============================================================================
	//	GraphValueType enum
	//	GraphのPinで扱う値の種類
	//============================================================================

	enum class GraphValueType {

		Unknown,

		Flow,
		Texture2D,
		Texture2DUAV,
		DepthTexture,
		RenderTarget,
		Buffer,
		View,

		Float,
		Float2,
		Float3,
		Float4,
		Int,
		UInt,
		Bool,
		String,

		Asset,
		MaterialAsset,
		SceneAsset,
		TransitionEffectAsset,
		ShaderAsset,
	};
} // Engine
