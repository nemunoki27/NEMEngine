#pragma once

namespace Engine {

	//============================================================================
	//	ConfigPaths
	//	RuntimePaths::GetGameConfigPathへ渡す設定ファイルの相対パスを一元管理する
	//	値はゲーム側Config配下の既存ファイル名と一致させること、変更すると既存設定が読めなくなる
	//============================================================================
	namespace ConfigPaths {

		// 入力デバイス設定
		inline constexpr const char* kInputDevice = "Config/inputDevice.exeConfig.json";
		// 描画フィーチャー切り替え設定
		inline constexpr const char* kGraphicsFeatureSettings = "Config/graphicsFeatureSettings.exeConfig.json";
		// シーンビューカメラの保存状態、ファイル名は旧称initExeDataのまま
		inline constexpr const char* kSceneViewCamera = "Config/initExeData.exeConfig.json";
		// 起動時に開くアクティブシーン
		inline constexpr const char* kActiveScene = "Config/activeScene.exeConfig.json";
		// フレームレート設定
		inline constexpr const char* kFrameRate = "Config/frameRate.exeConfig.json";
		// ビューポートパネルの表示状態
		inline constexpr const char* kViewportPanel = "Config/viewportPanel.exeConfig.json";
		// インスペクターのモデルプレビューカメラ
		inline constexpr const char* kInspectorModelPreviewCamera = "Config/inspectorModelPreviewCamera.exeConfig.json";
		// プロジェクトパネルの表示状態
		inline constexpr const char* kProjectPanel = "Config/projectPanel.exeConfig.json";
		// ユーザーが保存したエディターレイアウト
		inline constexpr const char* kEditorLayouts = "Config/editorLayouts.exeConfig.json";
		// 終了時のエディターレイアウト
		inline constexpr const char* kEditorLayoutSession = "Config/editorLayoutSession.exeConfig.json";

	} // ConfigPaths
} // Engine
