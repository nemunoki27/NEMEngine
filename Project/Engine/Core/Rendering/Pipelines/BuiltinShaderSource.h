#pragma once

//============================================================================
//	include
//============================================================================

namespace Engine::BuiltinShaderSource {

	//============================================================================
	//	Builtin shader source GUIDs
	// 移動に強い参照にするため、内蔵シェーダーは.metaのGUIDで固定参照する
	// 値はShaderCompileDesc::fileへ渡すソースファイル参照
	//============================================================================
	namespace Skybox {

		inline constexpr const char* VS = "5b1c0a7e3f9d2486";
		inline constexpr const char* PS = "7e2f4a9c1d8b6053";
	}

	namespace Line {

		inline constexpr const char* GeometryVS = "b2995658d93cd4ab";
		inline constexpr const char* GeometryGS = "0f434bd88135ee44";
		inline constexpr const char* GeometryPS = "bfddf777ae6b39b6";
		inline constexpr const char* AnalyticGridVS = "bec9516b4cd54de2";
		inline constexpr const char* AnalyticGridPS = "a31bb01681f8ac3d";
	}

	namespace Editor {

		inline constexpr const char* PickMeshInstanceCS = "8d50435034671c29";
		inline constexpr const char* SceneOverlaySpriteVS = "edf35b0e885ae326";
		inline constexpr const char* SceneOverlaySpritePS = "feaf5c3be1a811cf";
	}
}
