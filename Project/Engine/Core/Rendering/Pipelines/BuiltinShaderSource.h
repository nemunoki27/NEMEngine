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

		inline constexpr const char* VS = "4e454d41535345545b1c0a7e3f9d2486";
		inline constexpr const char* PS = "4e454d41535345547e2f4a9c1d8b6053";
	}

	namespace Line {

		inline constexpr const char* GeometryVS = "4e454d4153534554b2995658d93cd4ab";
		inline constexpr const char* GeometryGS = "4e454d41535345540f434bd88135ee44";
		inline constexpr const char* GeometryPS = "4e454d4153534554bfddf777ae6b39b6";
		inline constexpr const char* AnalyticGridVS = "4e454d4153534554bec9516b4cd54de2";
		inline constexpr const char* AnalyticGridPS = "4e454d4153534554a31bb01681f8ac3d";
	}

	namespace Editor {

		inline constexpr const char* PickMeshRasterPS = "4e454d41535345548d50435034671c2a";
		inline constexpr const char* SceneOverlaySpriteVS = "4e454d4153534554edf35b0e885ae326";
		inline constexpr const char* SceneOverlaySpritePS = "4e454d4153534554feaf5c3be1a811cf";
	}
}
