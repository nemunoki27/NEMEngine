#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Platform/Input/InputTypes.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	ViewportPanel enum class
	//============================================================================
	// 表示するビューポートの種類
	enum class ViewportPanelKind {

		Game,
		Scene,
	};

	// front
	class TextureUploadService;

	//============================================================================
	//	ViewportPanel class
	//	ビューの表示パネル
	//============================================================================
	class ViewportPanel :
		public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		ViewportPanel(const char* windowName, const char* label, ViewportPanelKind kind, TextureUploadService& textureUploadService);
		~ViewportPanel() = default;

		void Draw(const EditorPanelContext& context) override;

		//--------- accessor -----------------------------------------------------

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// ギズモ操作セッションの情報をまとめた構造体
		struct EntityGizmoSession {

			bool active = false;
			UUID entityUUID{};
			TransformComponent beforeTransform{};
		};
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

		std::string windowName_;
		std::string label_;
		ViewportPanelKind kind_ = ViewportPanelKind::Scene;

		ImVec2 viewSize_ = ImVec2(768.0f, 432.0f);

		// ギズモ操作セッションの情報
		EntityGizmoSession entityGizmoSession_{};

		TextureUploadService* textureUploadService_ = nullptr;

		// 表示アイコン
		IconSet icons_{};

		// アイコンボタンのサイズ
		const ImVec2 buttonSize_ = ImVec2(32.0f, 32.0f);

		//--------- functions ----------------------------------------------------

		void DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size);

		// シーンギズモの描画
		void DrawSceneGizmo(const EditorPanelContext& context);
		// ギズモ終了
		void FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world);

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

