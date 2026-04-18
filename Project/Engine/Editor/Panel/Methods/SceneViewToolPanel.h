#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>
#include <Engine/Core/Graphics/Texture/TextureUploadService.h>

namespace Engine {

	//============================================================================
	//	SceneViewToolPanel class
	//	シーンビューのツールパネル
	//============================================================================
	class SceneViewToolPanel :
		public IEditorPanel {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SceneViewToolPanel(TextureUploadService& textureUploadService);
		~SceneViewToolPanel() = default;

		void Draw(const EditorPanelContext& context) override;

		//--------- accessor -----------------------------------------------------

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// アイコン
		struct IconSet {

			// エンティティ選択機能のオン/オフ
			std::string enablePickKey;
			// エンティティ/サブメッシュを選択するだけ
			std::string noneKey;

			// マニュピレーター
			std::string translateKey;
			std::string rotateKey;
			std::string scaleKey;

			// エンティティ単位かサブメッシュ単位の選択を行うか
			std::string entitySelectKey;
			std::string subMeshSelectKey;

			std::string debugCameraKey;
			std::string entityCameraKey;
			std::string manualCamera2DKey;
			std::string manualCamera3DKey;
		};

		//--------- variables ----------------------------------------------------

		TextureUploadService* textureUploadService_ = nullptr;

		// 表示アイコン
		IconSet icons_{};

		// アイコンボタンのサイズ
		const ImVec2 buttonSize_ = ImVec2(32.0f, 32.0f);

		//--------- functions ----------------------------------------------------

		// アイコン読み込み
		void RequestIcons();
		// アイコンのテクスチャIDを取得
		ImTextureID GetTextureID(const std::string& key) const;

		bool DrawIconButton(const char* id, ImTextureID textureID, bool active, const ImVec2& size) const;
		void DrawCameraSection(const EditorPanelContext& context);
		void DrawManipulatorSection(const EditorPanelContext& context);
		void DrawEntityCameraPopup(const EditorPanelContext& context);
	};
} // Engine