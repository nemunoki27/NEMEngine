#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <string>
// imgui
#include <imgui.h>

namespace Engine {

	class TextureUploadService;
	struct EditorPanelContext;

	//============================================================================
	//	ViewportToolbar class
	//	Scene操作の表示とアイコンを管理する
	//============================================================================
	class ViewportToolbar {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ViewportToolbar(TextureUploadService& textureUploadService);

		// Scene操作のツール列を表示する
		void Draw(const EditorPanelContext& context);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 操作ごとのアイコン名
		struct IconSet {

			// 選択の有効切替
			std::string enablePickKey = "enablePickKey.dds";
			// 選択のみ
			std::string noneKey = "manipulatorNone.png";
			// 移動操作
			std::string translateKey = "manipulatorTranslate.png";
			// 回転操作
			std::string rotateKey = "manipulatorRotate.png";
			// 拡縮操作
			std::string scaleKey = "manipulatorScale.png";
			// デバッグCamera
			std::string debugCameraKey = "debugCamera.dds";
			// EntityCamera
			std::string entityCameraKey = "entityCamera.dds";
			// Entity選択
			std::string entitySelectKey = "entitySelect.dds";
			// SubMesh選択
			std::string subMeshSelectKey = "subMeshSelect.dds";
			// 2D選択
			std::string selection2DKey = "2DOnly.png";
			// 3D選択
			std::string selection3DKey = "3DOnly.png";
			// 2Dと3Dの選択
			std::string selection2DAnd3DKey = "2DAnd3D.png";
			// Grid表示
			std::string drawGridKey = "enabeDrawGrid.png";
			// 中心ピボット
			std::string gizmoCenterPivotKey = "gizmoCenterPivot.png";
			// 各Entityの原点
			std::string eachEntityOriginKey = "eachEntityOrigin.png";
			// スナップ操作
			std::string snapEditEntityKey = "snapEditEntity.png";
			// Prefab編集の表示切替
			std::string prefabExitKey = "scene.png";
		};

		//--------- variables ----------------------------------------------------

		// 操作Buttonのサイズ
		const ImVec2 buttonSize_ = ImVec2(24.0f, 24.0f);

		// Applicationが所有するTexture転送窓口
		TextureUploadService& textureUploadService_;
		// 操作アイコン
		IconSet icons_{};

		//--------- functions ----------------------------------------------------

		// アイコンの読込を要求する
		void RequestIcons();
		// アイコンの描画参照を取得する
		ImTextureID GetTextureID(const std::string& key) const;
		// Cameraの操作設定を表示する
		void DrawCameraSection(const EditorPanelContext& context);
		// 選択とGizmoの操作を表示する
		void DrawManipulatorSection(const EditorPanelContext& context);
		// スナップ単位を編集する
		void DrawSnapSettingsPopup(const EditorPanelContext& context);
		// Gridの表示を切り替える
		void DrawGridSection(const EditorPanelContext& context);
	};
} // Engine
