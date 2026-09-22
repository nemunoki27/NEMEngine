#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Debug/DepthVisualizer.h>

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

		// シーンギズモの描画
		void DrawSceneGizmo(const EditorPanelContext& context);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct EntityGizmoSession {

			bool active = false;
			bool runtimeOnly = false;
			UUID entityUUID{};
			TransformComponent beforeTransform{};
		};

		struct MultiEntityGizmoSession {

			bool active = false;
			bool runtimeOnly = false;
			// ドラッグ中フレーム間で持続する中心ピボット
			TransformComponent pivot{};
			// undo用の操作前トランスフォーム
			std::vector<std::pair<UUID, TransformComponent>> beforeTransforms{};
		};

		//--------- variables ----------------------------------------------------

		// ギズモ操作セッションの情報
		EntityGizmoSession entityGizmoSession_{};
		MultiEntityGizmoSession multiGizmoSession_{};

		//--------- functions ----------------------------------------------------

		// ギズモ終了
		void FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world);
		// 複数選択ギズモの描画、中心ピボットの差分を各エンティティへ個別原点で適用する
		void DrawMultiEntityGizmo(const EditorPanelContext& context, ECSWorld& world, const GizmoViewportRect& rect);
		// 複数EntityのGizmo操作を確定する
		void FinalizeMultiEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world);
	};
}
