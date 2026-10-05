#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/Utility/EditorTransformPreview.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

namespace Engine {

	struct GizmoViewportRect;

	//============================================================================
	//	ViewportGizmoSession class
	//	ギズモの開始値と編集確定を管理する
	//============================================================================
	class ViewportGizmoSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 未確定の姿勢を戻して操作を終了する
		void EndPreview();

		// シーンギズモの描画
		void DrawSceneGizmo(const EditorPanelContext& context);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct EntityGizmoSession {

			bool active = false; // 操作中か
			bool runtimeOnly = false; // 実行用Worldの操作か
			UUID entityUUID{}; // 開始時の対象
		};

		struct MultiEntityGizmoSession {

			bool active = false; // 操作中か
			bool runtimeOnly = false; // 実行用Worldの操作か
			// 操作中の中心ピボット
			TransformComponent pivot{};
		};

		//--------- variables ----------------------------------------------------

		// 開始Worldと対象の姿勢
		EditorTransformPreview preview_;

		// ギズモ操作セッションの情報
		EntityGizmoSession entityGizmoSession_{};
		MultiEntityGizmoSession multiGizmoSession_{};

		//--------- functions ----------------------------------------------------

		// 姿勢を一件の履歴へ確定する
		void FinalizePreview(const EditorPanelContext& context, ECSWorld& world, bool runtimeOnly);
		// ギズモ終了
		void FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world);
		// 複数選択の共通ピボットを操作する
		void DrawMultiEntityGizmo(const EditorPanelContext& context, ECSWorld& world, const GizmoViewportRect& rect);
		// 複数EntityのGizmo操作を確定する
		void FinalizeMultiEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world);
	};
}
