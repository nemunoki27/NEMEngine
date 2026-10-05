#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include "ViewportGizmoSession.h"
#include "ViewportDepthSurface.h"
#include "ViewportPlacementSession.h"
#include "ViewportToolbar.h"

// c++
#include <string>

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
	class ViewportPanel : public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ViewportPanel(
			const char* windowName, const char* label, ViewportPanelKind kind, TextureUploadService& textureUploadService);
		~ViewportPanel() = default;

		// 描画画像と操作領域を表示する
		void Draw(const EditorPanelContext& context) override;
		// 非表示とWorld切替の前に配置を終了する
		void EndPreview() override;

		//--------- accessor -----------------------------------------------------

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// ギズモの開始値と編集確定を管理する
		ViewportGizmoSession gizmoSession_;

		// 深度表示の描画資源を所有する
		ViewportDepthSurface depthSurface_;

		// 配置プレビューの仮エンティティを管理する
		ViewportPlacementSession placementSession_;

		// Scene操作とアイコンの表示
		ViewportToolbar toolbar_;

		// ImGuiのWindow名
		std::string windowName_;
		// 画像領域のID
		std::string label_;
		// GameとSceneの表示種別
		ViewportPanelKind kind_ = ViewportPanelKind::Scene;

		// 縦横比を保った画像サイズ
		ImVec2 viewSize_ = ImVec2(768.0f, 432.0f);

		//--------- functions ----------------------------------------------------

		// Viewport画像と操作を表示する
		void DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size);
	};
} // Engine
