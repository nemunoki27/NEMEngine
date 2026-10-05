#include "ViewportGizmoSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include "ViewportTransformUtility.h"

using namespace Engine::ViewportTransformUtility;

void Engine::ViewportGizmoSession::EndPreview() {

	// 開始Worldの編集だけを戻す
	preview_.Cancel();
	entityGizmoSession_ = {};
	multiGizmoSession_ = {};
}

void Engine::ViewportGizmoSession::DrawSceneGizmo(const EditorPanelContext& context) {

	// フラグリセット
	context.editorState->useSceneGizmo = false;

	ECSWorld* worldPtr = context.GetWorld();
	if (!worldPtr) {
		EndPreview();
		return;
	}
	ECSWorld& world = *worldPtr;
	// World切替後へ操作を持ち越さない
	if ((entityGizmoSession_.active || multiGizmoSession_.active) && !preview_.BelongsTo(world)) {
		EndPreview();
	}

	// Camera移動中は操作を終了する
	if (context.editorState->cameraFocusing) {
		FinalizeEntityGizmoSession(context, world);
		FinalizeMultiEntityGizmoSession(context, world);
		return;
	}

	// 描画できない場合は操作を終了する
	if (!context.sceneRenderView || !context.sceneRenderView->valid ||
		context.editorState->sceneViewManipulatorMode == SceneViewManipulatorMode::None) {
		FinalizeEntityGizmoSession(context, world);
		FinalizeMultiEntityGizmoSession(context, world);
		return;
	}
	// 現在のマニピュレーター操作を取得
	const ImVec2 rectMin = ImGui::GetItemRectMin();
	const ImVec2 rectMax = ImGui::GetItemRectMax();

	// ギズモの描画に必要な情報をまとめた構造体を作成
	GizmoViewportRect rect{};
	rect.x = rectMin.x;
	rect.y = rectMin.y;
	rect.width = rectMax.x - rectMin.x;
	rect.height = rectMax.y - rectMin.y;
	// 描画領域がなければ操作を終了する
	if (!rect.IsValid()) {
		FinalizeEntityGizmoSession(context, world);
		FinalizeMultiEntityGizmoSession(context, world);
		return;
	}

	//============================================================================
	//	エンティティ選択中
	//============================================================================
	{
		// 複数選択は共通ピボットへ切り替える
		if (context.editorState->SelectionCount() > 1) {

			FinalizeEntityGizmoSession(context, world);
			DrawMultiEntityGizmo(context, world, rect);
			return;
		}
		FinalizeMultiEntityGizmoSession(context, world);

		const Entity entity = context.editorState->selectedEntity;
		// 対象がなければ操作を終了する
		if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
			FinalizeEntityGizmoSession(context, world);
			return;
		}
		// 別セッションなら先に閉じる
		if (entityGizmoSession_.active && entityGizmoSession_.entityUUID != world.GetUUID(entity)) {
			FinalizeEntityGizmoSession(context, world);
		}

		// 現在のマニピュレーター操作を取得
		bool use2DTarget = Prefers2DGizmo(context, world, entity);
		const ResolvedCameraView* camera = SelectSceneGizmoCamera(*context.sceneRenderView, use2DTarget);
		if (!camera) {
			FinalizeEntityGizmoSession(context, world);
			return;
		}

		// ギズモの描画に必要な情報をまとめた構造体を作成
		GizmoViewContext gizmoContext{};
		gizmoContext.rect = rect;
		gizmoContext.viewMatrix = camera->matrices.viewMatrix;
		gizmoContext.projectionMatrix = camera->matrices.projectionMatrix;
		gizmoContext.parentWorldMatrix = GetEntityParentWorldMatrix(world, entity);
		gizmoContext.mode = context.editorState->sceneViewManipulatorMode;
		gizmoContext.orthographic = camera->projectionMode == ResolvedProjectionMode::Orthographic;
		gizmoContext.allowAxisFlip = !use2DTarget;

		// 操作と次元に合わせてスナップ幅を渡す
		const GridSnapAxis* snapAxis = nullptr;
		if (context.editorState->enableSnapEditEntity) {

			snapAxis = SelectSnapAxis(context.editorState->snapSettings, gizmoContext.mode, use2DTarget);
			if (snapAxis && snapAxis->size > 0.0f) {

				gizmoContext.useSnap = true;
				gizmoContext.snapValues[0] = snapAxis->size;
				gizmoContext.snapValues[1] = snapAxis->size;
				gizmoContext.snapValues[2] = snapAxis->size;
			}
		}

		TransformComponent previewTransform = world.GetComponent<TransformComponent>(entity);

		// ギズモを描画し、操作結果を取得する
		const GizmoEditResult result = use2DTarget
										   ? MyGUI::Manipulate2D("##SceneEntityGizmo2D", gizmoContext, previewTransform)
										   : MyGUI::Manipulate3D("##SceneEntityGizmo3D", gizmoContext, previewTransform);

		// 使用しているか
		context.editorState->useSceneGizmo = result.IsUse();

		if (result.isUsing && !entityGizmoSession_.active) {

			const Entity target[] = {entity};
			if (!preview_.Begin(world, target, context.IsPlaying())) {
				return;
			}
			entityGizmoSession_.active = true;
			entityGizmoSession_.runtimeOnly = context.IsPlaying();
			entityGizmoSession_.entityUUID = world.GetUUID(entity);
		}

		// 変更値をプレビューへ反映する
		if (result.valueChanged) {

			// 絶対スナップは最寄りの格子へ丸める
			if (snapAxis && snapAxis->absolute && snapAxis->size > 0.0f) {
				ApplyAbsoluteSnap(previewTransform, gizmoContext.mode, snapAxis->size);
			}
			TransformEditUtility::ApplyImmediate(world, entity, previewTransform);
		}
		// 操作終了時に履歴へ確定する
		if (entityGizmoSession_.active && !result.isUsing) {

			FinalizeEntityGizmoSession(context, world);
		}
	}
}

void Engine::ViewportGizmoSession::FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world) {

	if (!entityGizmoSession_.active) {
		return;
	}

	FinalizePreview(context, world, entityGizmoSession_.runtimeOnly);
	entityGizmoSession_ = {};
}

void Engine::ViewportGizmoSession::DrawMultiEntityGizmo(
	const EditorPanelContext& context, ECSWorld& world, const GizmoViewportRect& rect) {

	// 有効な対象から選択中心を求める
	std::vector<Entity> targets{};
	Vector3 centerSum = Vector3::AnyInit(0.0f);
	// 操作中は開始時の対象を固定する
	if (multiGizmoSession_.active) {
		for (const auto& [uuid, transform] : preview_.GetSnapshots()) {
			const Entity entity = world.FindByUUID(uuid);
			if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity)) {
				targets.push_back(entity);
				centerSum += world.GetComponent<TransformComponent>(entity).worldMatrix.GetTranslationValue();
			}
		}
	} else {
		for (Entity entity : HierarchyUtility::CollectLogicalRoots(world, context.editorState->GetSelectedEntities())) {
			if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity)) {
				targets.push_back(entity);
				centerSum += world.GetComponent<TransformComponent>(entity).worldMatrix.GetTranslationValue();
			}
		}
	}
	if (targets.empty()) {
		FinalizeMultiEntityGizmoSession(context, world);
		return;
	}
	const Vector3 center = centerSum / static_cast<float>(targets.size());

	// 開始対象の次元でCameraを選ぶ
	const bool use2DTarget = Prefers2DGizmo(context, world, targets.front());
	const ResolvedCameraView* camera = SelectSceneGizmoCamera(*context.sceneRenderView, use2DTarget);
	if (!camera) {
		FinalizeMultiEntityGizmoSession(context, world);
		return;
	}

	// 操作中はピボットの姿勢を維持する
	TransformComponent pivot{};
	if (multiGizmoSession_.active) {
		pivot = multiGizmoSession_.pivot;
	} else {
		pivot.localPos = center;
		pivot.localRotation = Quaternion::Identity();
		pivot.localScale = Vector3::AnyInit(1.0f);
	}
	const TransformComponent prevPivot = pivot;

	GizmoViewContext gizmoContext{};
	gizmoContext.rect = rect;
	gizmoContext.viewMatrix = camera->matrices.viewMatrix;
	gizmoContext.projectionMatrix = camera->matrices.projectionMatrix;
	gizmoContext.parentWorldMatrix = Matrix4x4::Identity();
	gizmoContext.mode = context.editorState->sceneViewManipulatorMode;
	gizmoContext.orthographic = camera->projectionMode == ResolvedProjectionMode::Orthographic;
	gizmoContext.allowAxisFlip = !use2DTarget;

	// 単体操作と同じスナップ幅を渡す
	// 共通ピボットの差分を格子へ合わせる
	const GridSnapAxis* snapAxis = nullptr;
	if (context.editorState->enableSnapEditEntity) {

		snapAxis = SelectSnapAxis(context.editorState->snapSettings, gizmoContext.mode, use2DTarget);
		if (snapAxis && snapAxis->size > 0.0f) {

			gizmoContext.useSnap = true;
			gizmoContext.snapValues[0] = snapAxis->size;
			gizmoContext.snapValues[1] = snapAxis->size;
			gizmoContext.snapValues[2] = snapAxis->size;
		}
	}

	const GizmoEditResult result = use2DTarget ? MyGUI::Manipulate2D("##SceneMultiGizmo2D", gizmoContext, pivot)
											   : MyGUI::Manipulate3D("##SceneMultiGizmo3D", gizmoContext, pivot);

	context.editorState->useSceneGizmo = result.IsUse();

	// 操作開始時の姿勢を保持する
	if (result.isUsing && !multiGizmoSession_.active) {

		if (!preview_.Begin(world, targets, context.IsPlaying())) {
			return;
		}
		multiGizmoSession_.active = true;
		multiGizmoSession_.runtimeOnly = context.IsPlaying();
	}

	// ピボットのフレーム差分を各エンティティへ個別原点で適用する
	if (multiGizmoSession_.active && result.valueChanged) {

		const Vector3 deltaPos = pivot.localPos - prevPivot.localPos;
		const Quaternion deltaRot = pivot.localRotation * Quaternion::Inverse(prevPivot.localRotation);
		const Vector3 deltaScale(prevPivot.localScale.x != 0.0f ? pivot.localScale.x / prevPivot.localScale.x : 1.0f,
			prevPivot.localScale.y != 0.0f ? pivot.localScale.y / prevPivot.localScale.y : 1.0f,
			prevPivot.localScale.z != 0.0f ? pivot.localScale.z / prevPivot.localScale.z : 1.0f);

		// 中心ピボットは位置も中心周りに動かす
		const bool pivotAtCenter = context.editorState->gizmoPivotAtCenter;
		const Vector3 pivotCenter = prevPivot.localPos;
		for (const Entity& entity : targets) {

			TransformComponent transform{};
			// 親の回転と拡縮を含めてWorld差分を戻す
			if (!ResolveWorldDelta(world, entity, deltaPos, deltaRot, deltaScale, pivotCenter, pivotAtCenter, transform)) {
				continue;
			}
			// 各Entityへ絶対スナップを適用する
			if (snapAxis && snapAxis->absolute && snapAxis->size > 0.0f) {
				ApplyAbsoluteSnap(transform, gizmoContext.mode, snapAxis->size);
			}
			TransformEditUtility::ApplyImmediate(world, entity, transform);
		}
	}

	if (multiGizmoSession_.active && !result.isUsing) {
		FinalizeMultiEntityGizmoSession(context, world);
	} else if (multiGizmoSession_.active) {
		multiGizmoSession_.pivot = pivot;
	}
}

void Engine::ViewportGizmoSession::FinalizeMultiEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world) {

	if (!multiGizmoSession_.active) {
		return;
	}
	FinalizePreview(context, world, multiGizmoSession_.runtimeOnly);
	multiGizmoSession_ = {};
}

void Engine::ViewportGizmoSession::FinalizePreview(const EditorPanelContext& context, ECSWorld& world, bool runtimeOnly) {

	// 実行中の操作は値を維持して終了する
	if (runtimeOnly) {
		preview_.Release();
		return;
	}
	if (!context.CanEditScene() || !context.host || !preview_.BelongsTo(world)) {
		preview_.Cancel();
		return;
	}

	// 仮の値を戻してから履歴へ確定する
	auto command = preview_.BuildCommand(world);
	preview_.Cancel();
	if (command) {
		context.host->ExecuteEditorCommand(std::move(command));
	}
}
