#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include "ViewportGizmoSession.h"
#include "ViewportDepthSurface.h"
#include "ViewportPlacementSession.h"
#include <Engine/Core/Platform/Input/InputTypes.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Debug/DepthVisualizer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

// c++
#include <memory>
#include <utility>
#include <vector>

#include <json.hpp>

namespace Engine {

	// front
	struct GizmoViewportRect;

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

		// 複数選択ギズモのセッション、中心ピボットを保持し各エンティティへ相対適用する

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
			std::string selection2DKey;
			std::string selection3DKey;
			std::string selection2DAnd3DKey;
			std::string drawGridKey;

			// 複数選択ギズモのピボット切り替え用、中心ピボットと各原点
			std::string gizmoCenterPivotKey;
			std::string eachEntityOriginKey;
			// オブジェクトのスナップ操作アイコン
			std::string snapEditEntityKey;
			// プレファブ編集を抜けて通常のシーン表示へ戻る用、編集中だけツール列の最上段に出す
			std::string prefabExitKey;
		};

		//--------- variables ----------------------------------------------------

		// ギズモの開始値と編集確定を管理する
		ViewportGizmoSession gizmoSession_;

		// 深度表示の描画資源を所有する
		ViewportDepthSurface depthSurface_;

		// 配置プレビューの仮エンティティを管理する
		ViewportPlacementSession placementSession_;

		std::string windowName_;
		std::string label_;
		ViewportPanelKind kind_ = ViewportPanelKind::Scene;

		ImVec2 viewSize_ = ImVec2(768.0f, 432.0f);

		TextureUploadService* textureUploadService_ = nullptr;

		// 表示アイコン
		IconSet icons_{};

		// アイコンボタンのサイズ
		const ImVec2 buttonSize_ = ImVec2(24.0f, 24.0f);

		//--------- functions ----------------------------------------------------

		// Viewport画像と操作を表示する
		void DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size);

		// アイコン読み込み
		void RequestIcons();
		// アイコンのテクスチャIDを取得
		ImTextureID GetTextureID(const std::string& key) const;

		// 状態を示すアイコンボタンを表示する
		bool DrawIconButton(const char* id, ImTextureID textureID, bool active, const ImVec2& size) const;
		// Cameraの操作設定を表示する
		void DrawCameraSection(const EditorPanelContext& context);
		// Gizmoの操作設定を表示する
		void DrawManipulatorSection(const EditorPanelContext& context);
		// スナップ設定の右クリックポップアップ、SRTのグリッド単位と絶対スナップ、グリッド表示を編集する
		void DrawSnapSettingsPopup(const EditorPanelContext& context);
		// Gridの表示設定を表示する
		void DrawGridSection(const EditorPanelContext& context);
		// 表示するEntity Cameraを選択する
		void DrawEntityCameraPopup(const EditorPanelContext& context);
	};
} // Engine
