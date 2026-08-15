#pragma once

namespace Engine {

	//============================================================================
	//	RenderTargetNames
	//	SceneMainのGBuffer各アタッチメントと最終カラーの識別名を一元管理する
	//	RenderPathResourcesの生成名、RenderPipelineRunnerの登録名、PostProcessの入力名は
	//	すべてここを参照して一致させること、名前がずれると名前解決でSRVが白テクスチャに落ちる
	//============================================================================
	namespace RenderTargetNames {

		// GBuffer: ベースカラー
		inline constexpr const char* kSceneColorMain = "SceneColorMain";
		// GBuffer: ワールド法線
		inline constexpr const char* kSceneNormalMain = "SceneNormalMain";
		// GBuffer: ワールド座標
		inline constexpr const char* kScenePositionMain = "ScenePositionMain";
		// GBuffer: 質感パラメータ(metallic/roughness/occlusion)
		inline constexpr const char* kSceneMaterialMain = "SceneMaterialMain";
		// GBuffer: 自己発光
		inline constexpr const char* kSceneEmissiveMain = "SceneEmissiveMain";
		// GBuffer: マテリアル挙動フラグ
		inline constexpr const char* kSceneFlagsMain = "SceneFlagsMain";
		// GBuffer: 現在UVから前フレームUVへの移動量
		inline constexpr const char* kSceneMotionMain = "SceneMotionMain";
		// シーン深度
		inline constexpr const char* kSceneDepth = "SceneDepth";
		// ライティング/レイトレ後の最終カラー
		inline constexpr const char* kSceneColorFinal = "SceneColorFinal";
		// 透明描画開始直前のSceneColorFinalコピー
		inline constexpr const char* kSceneColorOpaque = "SceneColorOpaque";

	} // RenderTargetNames
} // Engine
