#include "TransformInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Editor/Command/Methods/SetTransformCommand.h>
#include <Engine/Editor/Command/TransformEditUtility.h>
#include <Engine/Editor/Panel/Interface/IEditorPanelHost.h>
#include <Engine/Utility/ImGui/MyGUI.h>
#include <Engine/Utility/Enum/EnumAdapter.h>

//============================================================================
//	TransformInspectorDrawer classMethods
//============================================================================

namespace {

	// ワールドのトランスフォームとドラフトのトランスフォームを比較して、どこかが変化しているかどうかを判定する
	bool HasExternalTransformChanged(const Engine::TransformComponent& worldTransform,
		const Engine::TransformComponent& draftTransform) {

		return !Engine::Vector3::NearlyEqual(worldTransform.localPos, draftTransform.localPos) ||
			!Engine::Quaternion::NearlyEqual(worldTransform.localRotation, draftTransform.localRotation) ||
			!Engine::Vector3::NearlyEqual(worldTransform.localScale, draftTransform.localScale);
	}
}

void Engine::TransformInspectorDrawer::Draw(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	// 描画できない場合は何もしない
	if (!CanDraw(world, entity)) {
		return;
	}

	// ドラフトのエンティティが変わった、または編集状態でない場合はワールドからドラフトを更新する
	const UUID stableUUID = world.GetUUID(entity);
	bool shouldSyncFromWorld = (editingEntityStableUUID_ != stableUUID);
	if (!shouldSyncFromWorld && !isEditing_) {

		const auto& worldTransform = world.GetComponent<TransformComponent>(entity);
		shouldSyncFromWorld = HasExternalTransformChanged(worldTransform, draftTransform_);
	}
	if (shouldSyncFromWorld) {
		SyncDraftFromWorld(world, entity);
	}

	if (!MyGUI::CollapsingHeader("Transform")) {
		return;
	}

	// アイテムを操作しているか
	bool anyItemActive = false;

	ImGui::PushItemWidth(160.0f);
	EnumAdapter<Dimension>::Combo("Dimension", &editDimension_);
	ImGui::PopItemWidth();

	bool is3D = editDimension_ == Dimension::Type3D;

	//============================================================================
	//	座標編集
	//============================================================================
	{
		float dragSpeed = is3D ? 0.01f : 1.0f;
		auto editResult = MyGUI::DragVector3("Position", draftTransform_.localPos, { .dragSpeed = dragSpeed,.minValue = -10000.0f,.maxValue = 10000.0f });
		if (editResult.valueChanged) {
			draftTransform_.isDirty = true;
			previewRequested_ = true;
		}
		anyItemActive |= editResult.anyItemActive;
		commitRequested_ |= editResult.editFinished;
		ImGui::Separator();
	}
	//============================================================================
	//	回転編集
	//============================================================================
	{
		ValueEditResult editResult{};
		if (is3D) {

			editResult = MyGUI::DragVector3("Rotation", draftEulerDegrees_, { .dragSpeed = 0.1f });
		} else {

			editResult = MyGUI::DragFloat("Rotation", draftEulerDegrees_.z, { .dragSpeed = 0.1f });
		}
		if (editResult.valueChanged) {

			draftTransform_.isDirty = true;
			previewRequested_ = true;
		}
		anyItemActive |= editResult.anyItemActive;
		commitRequested_ |= editResult.editFinished;
		ImGui::Separator();
	}
	//============================================================================
	//	スケール編集
	//============================================================================
	{
		ValueEditResult editResult{};
		if (is3D) {

			editResult = MyGUI::DragVector3("Scale", draftTransform_.localScale, { .dragSpeed = 0.01f });
		} else {

			Vector2 scale2D{ draftTransform_.localScale.x, draftTransform_.localScale.y };
			editResult = MyGUI::DragVector2("Scale", scale2D, { .dragSpeed = 0.01f });
			draftTransform_.localScale.x = scale2D.x;
			draftTransform_.localScale.y = scale2D.y;
		}
		if (editResult.valueChanged) {
			draftTransform_.isDirty = true;
			previewRequested_ = true;
		}
		anyItemActive |= editResult.anyItemActive;
		commitRequested_ |= editResult.editFinished;
		ImGui::Separator();
	}

	// プレビューが必要なら適用
	ApplyPreviewIfNeeded(world, entity);

	//============================================================================
	//	Matrix
	//============================================================================

	MyGUI::TextMatrix4x4("World Matrix", draftTransform_.worldMatrix);

	// アイテムを操作している場合は編集状態にする
	isEditing_ = anyItemActive;

	// 編集内容の同期
	CommitTransformIfNeeded(context, world, entity);
}

bool Engine::TransformInspectorDrawer::CanDraw(ECSWorld& world, const Entity& entity) const {

	return world.HasComponent<TransformComponent>(entity);
}

void Engine::TransformInspectorDrawer::SyncDraftFromWorld(ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
		return;
	}

	const UUID stableUUID = world.GetUUID(entity);
	const auto& transform = world.GetComponent<TransformComponent>(entity);

	const Vector3 rawEulerDegrees = Quaternion::ToEulerAngles(transform.localRotation);

	draftTransform_ = transform;
	draftTransform_.worldMatrix = transform.worldMatrix;

	if (editingEntityStableUUID_ == stableUUID) {
		draftEulerDegrees_ = Vector3::MakeContinuousDegrees(rawEulerDegrees, draftEulerDegrees_);
	} else {
		draftEulerDegrees_ = rawEulerDegrees;
	}

	editingEntityStableUUID_ = stableUUID;

	previewActive_ = false;
	previewRequested_ = false;
	previewBeginTransform_ = {};
}

void Engine::TransformInspectorDrawer::CommitTransformIfNeeded(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity) {

	if (!commitRequested_) {
		return;
	}
	commitRequested_ = false;

	// エンティティが存在しない、またはトランスフォームコンポーネントがない場合は何もしない
	if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
		return;
	}

	// 変更前トランスフォーム
	const TransformComponent beforeTransform = world.GetComponent<TransformComponent>(entity);
	// 変更後トランスフォーム
	TransformComponent afterTransform = draftTransform_;
	afterTransform.localRotation = Quaternion::Normalize(Quaternion::EulerToQuaternion(draftEulerDegrees_));
	afterTransform.isDirty = true;

	// トランスフォームが変更されていない場合はコマンドを実行せず、ドラフトをワールドから再同期する
	if (SetTransformCommand::NearlyEqualTransform(beforeTransform, afterTransform)) {

		previewActive_ = false;
		draftTransform_ = afterTransform;
		return;
	}

	// コマンドを作成して実行
	context.host->ExecuteEditorCommand(std::make_unique<SetTransformCommand>(entity, beforeTransform, afterTransform));

	previewActive_ = false;
	if (world.IsAlive(entity) && world.HasComponent<TransformComponent>(entity)) {

		draftTransform_ = world.GetComponent<TransformComponent>(entity);
	} else {

		draftTransform_ = afterTransform;
	}
}

void Engine::TransformInspectorDrawer::ApplyPreviewIfNeeded(ECSWorld& world, const Entity& entity) {

	if (!previewRequested_) {
		return;
	}
	previewRequested_ = false;

	if (!world.IsAlive(entity) || !world.HasComponent<TransformComponent>(entity)) {
		return;
	}
	// プレビューがアクティブでない場合はプレビュー開始時のトランスフォームを保存する
	if (!previewActive_) {
		previewBeginTransform_ = world.GetComponent<TransformComponent>(entity);
		previewActive_ = true;
	}

	// プレビュー用のトランスフォームを作成してワールドに即座に適用する
	TransformComponent previewTransform = draftTransform_;
	previewTransform.localRotation = Quaternion::Normalize(Quaternion::EulerToQuaternion(draftEulerDegrees_));
	previewTransform.isDirty = true;
	if (TransformEditUtility::ApplyImmediate(world, entity, previewTransform)) {

		draftTransform_ = previewTransform;
	}
}