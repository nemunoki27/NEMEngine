#include "ViewportGizmoSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Editor/Commands/Transform/SetTransformCommand.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include "ViewportTransformUtility.h"

// c++
#include <cmath>
#include <algorithm>
#include <optional>


using namespace Engine::ViewportTransformUtility;

void Engine::ViewportGizmoSession::DrawSceneGizmo(const EditorPanelContext& context) {

	// フラグリセット
	context.editorState->useSceneGizmo = false;

	ECSWorld* worldPtr = context.GetWorld();
	if (!worldPtr) {
		return;
	}
	ECSWorld& world = *worldPtr;

	// フォーカスで寄っている最中はギズモを操作させない、ダブルクリックでの誤移動を防ぐ
	if (context.editorState->cameraFocusing) {
		FinalizeEntityGizmoSession(context, world);
		return;
	}

	// SceneViewを描画できない場合はギズモセッションを終了して何もしない
	if (!context.sceneRenderView || !context.sceneRenderView->valid ||
		context.editorState->sceneViewManipulatorMode == SceneViewManipulatorMode::None) {
		FinalizeEntityGizmoSession(context, world);
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
	// 描画領域が有効でない場合はギズモセッションを終了して何もしない
	if (!rect.IsValid()) {
		FinalizeEntityGizmoSession(context, world);
		return;
	}

	//============================================================================
	//	エンティティ選択中
	//============================================================================
	{
		// 複数選択中は中心ピボットで各エンティティを個別原点で動かすギズモへ切り替える
		if (context.editorState->SelectionCount() > 1) {

			FinalizeEntityGizmoSession(context, world);
			DrawMultiEntityGizmo(context, world, rect);
			return;
		}
		FinalizeMultiEntityGizmoSession(context, world);

		const Entity entity = context.editorState->selectedEntity;
		// 編集不可なエンティティの場合はギズモセッションを終了して何もしない
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
		gizmoContext.orthographic = camera == &context.sceneRenderView->orthographic;
		gizmoContext.allowAxisFlip = !use2DTarget;

		// スナップ有効時はmodeと次元に応じたグリッド単位をImGuizmoへ渡す
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
		const GizmoEditResult result = use2DTarget ? MyGUI::Manipulate2D("##SceneEntityGizmo2D", gizmoContext, previewTransform) :
			MyGUI::Manipulate3D("##SceneEntityGizmo3D", gizmoContext, previewTransform);

		// 使用しているか
		context.editorState->useSceneGizmo = result.IsUse();

		if (result.isUsing && !entityGizmoSession_.active) {

			entityGizmoSession_.active = true;
			entityGizmoSession_.runtimeOnly = context.IsPlaying();
			entityGizmoSession_.entityUUID = world.GetUUID(entity);
			entityGizmoSession_.beforeTransform = world.GetComponent<TransformComponent>(entity);
		}

		// 値が変更された場合はプレビュー設定をエンティティに適用する
		if (result.valueChanged) {

			// 絶対スナップ指定なら、操作対象の成分を最寄りのグリッドへ丸めてから適用する
			if (snapAxis && snapAxis->absolute && snapAxis->size > 0.0f) {
				ApplyAbsoluteSnap(previewTransform, gizmoContext.mode, snapAxis->size);
			}
			TransformEditUtility::ApplyImmediate(world, entity, previewTransform);
		}
		// 使用を終了した場合はセッションを終了する
		if (entityGizmoSession_.active && !result.isUsing) {

			FinalizeEntityGizmoSession(context, world);
		}
	}
}

void Engine::ViewportGizmoSession::FinalizeEntityGizmoSession(const EditorPanelContext& context, ECSWorld& world) {

	if (!entityGizmoSession_.active) {
		return;
	}

	// Play中はPlayWorldだけを変更し、EditWorldのUndo履歴には記録しない
	if (!entityGizmoSession_.runtimeOnly && !context.IsPlaying() && context.host) {
		const Entity entity = world.FindByUUID(entityGizmoSession_.entityUUID);
		if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity)) {

			const TransformComponent afterTransform = world.GetComponent<TransformComponent>(entity);
			// トランスフォームの値が変更されている場合はコマンドを実行して変更を記録する
			if (!SetTransformCommand::NearlyEqualTransform(entityGizmoSession_.beforeTransform, afterTransform)) {

				context.host->ExecuteEditorCommand(std::make_unique<SetTransformCommand>(entity,
					entityGizmoSession_.beforeTransform, afterTransform));
			}
		}
	}
	entityGizmoSession_ = {};
}

void Engine::ViewportGizmoSession::DrawMultiEntityGizmo(const EditorPanelContext& context, ECSWorld& world,
	const GizmoViewportRect& rect) {

	// 生存かつTransformを持つ対象だけ集め、中心を求める
	std::vector<Entity> targets{};
	Vector3 centerSum = Vector3::AnyInit(0.0f);
	for (const Entity& entity : context.editorState->GetSelectedEntities()) {

		if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity)) {
			targets.push_back(entity);
			centerSum += world.GetComponent<TransformComponent>(entity).worldMatrix.GetTranslationValue();
		}
	}
	if (targets.size() < 2) {
		FinalizeMultiEntityGizmoSession(context, world);
		return;
	}
	const Vector3 center = centerSum / static_cast<float>(targets.size());

	// 次元はアクティブなエンティティに合わせる、選択は同次元なので代表でよい
	const bool use2DTarget = Prefers2DGizmo(context, world, context.editorState->selectedEntity);
	const ResolvedCameraView* camera = SelectSceneGizmoCamera(*context.sceneRenderView, use2DTarget);
	if (!camera) {
		FinalizeMultiEntityGizmoSession(context, world);
		return;
	}

	// ドラッグ中はピボットを持続させ、idleは中心へ単位姿勢で置く
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
	gizmoContext.orthographic = camera == &context.sceneRenderView->orthographic;
	gizmoContext.allowAxisFlip = !use2DTarget;

	// スナップ有効時は単体ギズモと同様にmodeと次元に応じたグリッド単位をImGuizmoへ渡す
	// ピボットの差分がグリッド単位に丸まるので各エンティティの移動もグリッド刻みになる
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

	const GizmoEditResult result = use2DTarget ?
		MyGUI::Manipulate2D("##SceneMultiGizmo2D", gizmoContext, pivot) :
		MyGUI::Manipulate3D("##SceneMultiGizmo3D", gizmoContext, pivot);

	context.editorState->useSceneGizmo = result.IsUse();

	// ドラッグ開始時にundo用の操作前姿勢を控える
	if (result.isUsing && !multiGizmoSession_.active) {

		multiGizmoSession_.active = true;
		multiGizmoSession_.runtimeOnly = context.IsPlaying();
		multiGizmoSession_.beforeTransforms.clear();
		for (const Entity& entity : targets) {
			multiGizmoSession_.beforeTransforms.emplace_back(
				world.GetUUID(entity), world.GetComponent<TransformComponent>(entity));
		}
	}

	// ピボットのフレーム差分を各エンティティへ個別原点で適用する
	if (multiGizmoSession_.active && result.valueChanged) {

		const Vector3 deltaPos = pivot.localPos - prevPivot.localPos;
		const Quaternion deltaRot = pivot.localRotation * Quaternion::Inverse(prevPivot.localRotation);
		const Vector3 deltaScale(
			prevPivot.localScale.x != 0.0f ? pivot.localScale.x / prevPivot.localScale.x : 1.0f,
			prevPivot.localScale.y != 0.0f ? pivot.localScale.y / prevPivot.localScale.y : 1.0f,
			prevPivot.localScale.z != 0.0f ? pivot.localScale.z / prevPivot.localScale.z : 1.0f);

		// 中心ピボットなら位置を中心周りにorbitさせ、個別原点なら位置はそのままにする
		const bool pivotAtCenter = context.editorState->gizmoPivotAtCenter;
		const Vector3 pivotCenter = prevPivot.localPos;
		for (const Entity& entity : targets) {

			TransformComponent transform = world.GetComponent<TransformComponent>(entity);
			// 移動は共通デルタ
			transform.localPos = transform.localPos + deltaPos;
			if (pivotAtCenter) {

				// 回転と拡縮で位置を選択中心周りに動かす、modeは排他なので片方は単位
				const Vector3 offset = transform.localPos - pivotCenter;
				transform.localPos = pivotCenter + RotateVectorByQuaternion(deltaRot, offset) * deltaScale;
			}
			// 回転と拡縮は各自のトランスフォームへ相対適用する
			transform.localRotation = Quaternion::Normalize(deltaRot * transform.localRotation);
			transform.localScale = transform.localScale * deltaScale;
			// 絶対スナップ指定なら各エンティティの成分を最寄りのグリッドへ丸める、単体と同じ挙動
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
	// Play中はPlayWorldだけを変更し、EditWorldのUndo履歴には記録しない
	if (!multiGizmoSession_.runtimeOnly && !context.IsPlaying() && context.host && context.editorState) {

		// SetTransformCommandは非アクティブ対象を単一選択へ戻すため、複数選択を退避して後で復元する
		const std::vector<Entity> savedSelection = context.editorState->GetSelectedEntities();

		// 操作前後で変化したエンティティだけまとめてコマンド化する
		for (const auto& [uuid, beforeTransform] : multiGizmoSession_.beforeTransforms) {

			const Entity entity = world.FindByUUID(uuid);
			if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
				continue;
			}
			const TransformComponent afterTransform = world.GetComponent<TransformComponent>(entity);
			if (!SetTransformCommand::NearlyEqualTransform(beforeTransform, afterTransform)) {

				context.host->ExecuteEditorCommand(
					std::make_unique<SetTransformCommand>(entity, beforeTransform, afterTransform));
			}
		}
		// 退避していた複数選択を復元する
		context.editorState->SetSelectedEntities(savedSelection);
	}
	multiGizmoSession_ = {};
}
