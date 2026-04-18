#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>
#include <Engine/Input/InputStructures.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/UUID/UUID.h>

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

	//============================================================================
	//	ViewportPanel class
	//	ビューの表示パネル
	//============================================================================
	class ViewportPanel :
		public IEditorPanel {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ViewportPanel(const char* windowName, const char* label, ViewportPanelKind kind);
		~ViewportPanel() = default;

		void Draw(const EditorPanelContext& context) override;

		//--------- accessor -----------------------------------------------------

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// ギズモ操作セッションの情報をまとめた構造体
		struct EntityGizmoSession {

			bool active = false;
			UUID entityUUID{};
			TransformComponent beforeTransform{};
		};
		// サブメッシュギズモ操作セッションの情報をまとめた構造体
		struct SubMeshGizmoSession {

			bool active = false;
			UUID entityUUID{};
			UUID subMeshStableID{};
			nlohmann::json beforeMeshRenderer{};
		};

		//--------- variables ----------------------------------------------------

		std::string windowName_;
		std::string label_;
		ViewportPanelKind kind_ = ViewportPanelKind::Scene;

		ImVec2 viewSize_ = ImVec2(768.0f, 432.0f);

		// ギズモ操作セッションの情報
		EntityGizmoSession entityGizmoSession_{};
		SubMeshGizmoSession subMeshGizmoSession_{};

		//--------- functions ----------------------------------------------------

		void DrawViewportContent(const EditorPanelContext& context, const char* id, const ImVec2& size);
		void SyncInputViewRect(InputViewArea area, const ImVec2& screenPos, const ImVec2& drawSize, const Vector2& srcSize) const;

		// シーンギズモの描画
		void DrawSceneGizmo(const EditorPanelContext& context);
		// ギズモ終了
		void FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world);
		void FinalizeSubMeshGizmoSession(const EditorPanelContext& context, ECSWorld& world);
	};
} // Engine