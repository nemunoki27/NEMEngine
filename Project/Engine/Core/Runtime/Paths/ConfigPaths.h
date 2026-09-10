#pragma once

namespace Engine {

	//============================================================================
	//	ConfigPaths
	//	ProjectSettingsとUserSettingsへ渡す設定ファイルの相対パスを一元管理する
	//============================================================================
	namespace ConfigPaths {

		// 入力デバイス設定
		inline constexpr const char* kInputDevice = "Runtime/InputDevice.json";
		// 描画フィーチャー切り替え設定
		inline constexpr const char* kGraphicsFeatureSettings = "Runtime/GraphicsFeatures.json";
		// シーンビューカメラの保存状態
		inline constexpr const char* kSceneViewCamera = "Editor/SceneViewCamera.json";
		// 起動時に開くアクティブシーン
		inline constexpr const char* kActiveScene = "Editor/ActiveScene.json";
		// フレームレート設定
		inline constexpr const char* kFrameRate = "Runtime/FrameRate.json";
		// C#スクリプト型ごとの実行順上書き
		inline constexpr const char* kScriptExecutionOrder = "ScriptExecutionOrder.json";
		// 全シーン共通の衝突タイプと組み合わせ
		inline constexpr const char* kCollisionSettings = "CollisionSettings.json";
		// 製品へ引き継ぐプロジェクト設定
		inline constexpr const char* ProductSettings[] = {
			"InputActions.json", "TagSettings.json", "RenderingLayers.json",
			kScriptExecutionOrder, kCollisionSettings, kFrameRate,
		};
		// ビューポートパネルの表示状態
		inline constexpr const char* kViewportPanel = "Editor/ViewportPanel.json";
		// インスペクターのモデルプレビューカメラ
		inline constexpr const char* kInspectorModelPreviewCamera = "Editor/InspectorModelPreviewCamera.json";
		// プロジェクトパネルの表示状態
		inline constexpr const char* kProjectPanel = "Editor/ProjectPanel.json";
		// パフォーマンスチェックツールの設定
		inline constexpr const char* kPerformanceCheckTool =
			"Editor/performanceCheckTool.exeConfig.json";
		// シェーダーグラフの外観設定
		inline constexpr const char* kShaderGraphAppearance =
			"Editor/ShaderGraphAppearance.json";
		// ユーザーが保存したエディターレイアウト
		inline constexpr const char* kEditorLayouts = "Editor/Layouts.json";
		// 終了時のエディターレイアウト
		inline constexpr const char* kEditorLayoutSession = "Editor/LayoutSession.json";
		// 製品名と起動時フルスクリーン
		inline constexpr const char* kGameBuild = "Runtime/Game.json";
		// 製品起動時のシーン
		inline constexpr const char* kStartupScene = "Runtime/StartupScene.json";

	} // ConfigPaths
} // Engine
