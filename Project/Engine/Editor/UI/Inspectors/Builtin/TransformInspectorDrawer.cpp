#include "TransformInspectorDrawer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Editor/Commands/Transform/SetTransformCommand.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

//============================================================================
//	TransformInspectorDrawer classMethods
//============================================================================
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
		// ワールド側のtransformが外部で変化したらドラフトを同期する
		shouldSyncFromWorld = !SetTransformCommand::NearlyEqualTransform(worldTransform, draftTransform_);
	}
	if (shouldSyncFromWorld) {
		SyncDraftFromWorld(world, entity);
	}

	if (!MyGUI::CollapsingHeader("Transform")) {
		return;
	}

	// アイテムを操作しているか
	bool anyItemActive = false;

	auto dimensionResult = MyGUI::EnumCombo("次元", draftTransform_.dimension);
	if (dimensionResult.valueChanged) {
		previewRequested_ = true;
	}
	anyItemActive |= dimensionResult.anyItemActive;
	commitRequested_ |= dimensionResult.editFinished;

	bool is3D = draftTransform_.dimension == Dimension::Type3D;

	// ドラッグ編集の設定
	FloatEditSetting editSetting{ .minValue = -10000.0f,.maxValue = 10000.0f,.closeOnProperty = false,.reserveRightWidth = 80.0f };
	// ボタンサイズ
	ImVec2 resetButtonSize = ImVec2(editSetting.reserveRightWidth, ImGui::GetFrameHeight());

	//============================================================================
	//	座標編集
	//============================================================================
	{
		editSetting.dragSpeed = is3D ? 0.01f : 1.0f;

		auto editResult = MyGUI::DragVector3("位置", draftTransform_.localPos, editSetting);
		// リセット
		ImGui::SameLine();
		if (ImGui::Button("リセット##DragPosition", resetButtonSize)) {
			draftTransform_.localPos.Init();
			editResult.valueChanged = true;
			editResult.editFinished = true;
		}
		MyGUI::EndPropertyRow();
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
		editSetting.dragSpeed = 0.1f;

		ValueEditResult editResult{};
		if (is3D) {

			editResult = MyGUI::DragVector3("回転", draftEulerDegrees_, editSetting);
		} else {

			editSetting.floatAxis = Axis::Z;
			editResult = MyGUI::DragFloat("回転", draftEulerDegrees_.z, editSetting);
			editSetting.floatAxis = std::nullopt;
		}
		// リセット
		ImGui::SameLine();
		if (ImGui::Button("リセット##DragRotation", resetButtonSize)) {
			draftEulerDegrees_.Init();
			editResult.valueChanged = true;
			editResult.editFinished = true;
		}
		MyGUI::EndPropertyRow();
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
		editSetting.dragSpeed = 0.01f;

		ValueEditResult editResult{};
		if (is3D) {

			editResult = MyGUI::DragVector3("スケール", draftTransform_.localScale, editSetting);
		} else {

			Vector2 scale2D{ draftTransform_.localScale.x, draftTransform_.localScale.y };
			editResult = MyGUI::DragVector2("スケール", scale2D, editSetting);
			draftTransform_.localScale.x = scale2D.x;
			draftTransform_.localScale.y = scale2D.y;
		}
		// リセット
		ImGui::SameLine();
		if (ImGui::Button("リセット##DragScale", resetButtonSize)) {
			draftTransform_.localScale = Vector3::AnyInit(1.0f);
			editResult.valueChanged = true;
			editResult.editFinished = true;
		}
		MyGUI::EndPropertyRow();
		if (editResult.valueChanged) {
			draftTransform_.isDirty = true;
			previewRequested_ = true;
		}
		anyItemActive |= editResult.anyItemActive;
		commitRequested_ |= editResult.editFinished;
		ImGui::Separator();
	}

	//============================================================================
	//	親追従設定
	//============================================================================
	{
		// 親エンティティやスキンメッシュのジョイントへ追従する際、回転とスケールを任意で無視できる
		// 座標は常に追従するので位置の無視フラグは設けない
		bool changed = false;
		changed |= MyGUI::Checkbox("親の回転を無視", draftTransform_.ignoreParentRotation);
		changed |= MyGUI::Checkbox("親のスケールを無視", draftTransform_.ignoreParentScale);
		if (changed) {

			draftTransform_.isDirty = true;
			previewRequested_ = true;
			commitRequested_ = true;
		}
		ImGui::Separator();
	}

	// プレビューが必要なら適用
	ApplyPreviewIfNeeded(world, entity);

	//============================================================================
	//	行列
	//============================================================================

	MyGUI::TextMatrix4x4("ワールド行列", draftTransform_.worldMatrix);

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
	const TransformComponent beforeTransform = previewActive_ ? previewBeginTransform_ : world.GetComponent<TransformComponent>(entity);
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
