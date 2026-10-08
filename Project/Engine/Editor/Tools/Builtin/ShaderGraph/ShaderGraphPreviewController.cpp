#include "ShaderGraphPreviewController.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphEditSession.h"
#include "ShaderGraphScenePreview.h"
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
// imgui
#include <imgui.h>

Engine::ShaderGraphPreviewController::ShaderGraphPreviewController(
	ShaderGraphEditSession& session, ShaderGraphScenePreview& preview)
	: editSession_(session), scenePreview_(preview) {
}

void Engine::ShaderGraphPreviewController::DrawSettings(const EditorToolContext& context) {

	ImGui::SeparatorText("マテリアルプレビュー");

	// Scene上のプレビュー対象を選択する
	ECSWorld* world = context.GetWorld();
	UUID nextEntity = scenePreview_.GetTargetEntityUUID();
	MyGUI::ScopedPropertyLabelWidth labelWidth("ShaderGraphMaterialPreview");
	MyGUI::BeginPropertyRow("プレビューエンティティ");
	const float clearButtonWidth = 72.0f;
	const float fieldWidth =
		(std::max)(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);
	const ValueEditResult result = MyGUI::EntityReferenceField("", nextEntity, world,
		{
			.useAutoPropertyRow = false,
			.buttonSize = ImVec2(fieldWidth, ImGui::GetFrameHeight()),
		});
	ImGui::SameLine();
	const bool hasPreviewEntity = scenePreview_.GetTargetEntityUUID() != UUID{};
	ImGui::BeginDisabled(!hasPreviewEntity);
	const bool clear = ImGui::Button("解除", ImVec2(clearButtonWidth, ImGui::GetFrameHeight()));
	ImGui::EndDisabled();
	MyGUI::EndPropertyRow();

	if (clear) {
		Restore();
		scenePreview_.SetTargetEntityUUID({});
		return;
	}
	if (!result.valueChanged || nextEntity == scenePreview_.GetTargetEntityUUID()) {

		return;
	}

	// 元のMaterialを復元してから対象を切り替える
	Restore();
	scenePreview_.SetTargetEntityUUID(nextEntity);
	if (!scenePreview_.GetTargetEntityUUID()) {
		return;
	}
	if ((!editSession_.GetPreviewMaterialID() || editSession_.NeedsCompile()) && !editSession_.CompilePreview(context)) {

		return;
	}
	Apply(context);
}

void Engine::ShaderGraphPreviewController::Update(const EditorToolContext& context) {

	// 外部のWorld切替と対象の削除を確認する
	scenePreview_.SynchronizeWorld(context.GetWorld());

	if (editSession_.GetDraft().domain != ShaderGraphDomain::Surface) {
		compileDeadline_ = 0.0;
		return;
	}
	if (!scenePreview_.GetTargetEntityUUID()) {
		compileDeadline_ = 0.0;
		return;
	}
	if (!editSession_.NeedsCompile()) {
		compileDeadline_ = 0.0;
		if (!scenePreview_.IsMaterialApplied()) {
			Apply(context);
		}
		return;
	}
	if (ImGui::IsAnyItemActive()) {
		compileDeadline_ = 0.0;
		return;
	}

	// 入力確定後のコンパイル時刻を決める
	const double now = ImGui::GetTime();
	if (compileDeadline_ <= 0.0) {
		compileDeadline_ = now + 0.25;
		return;
	}
	if (now < compileDeadline_) {
		return;
	}

	// 成功したプレビューMaterialだけを反映する
	compileDeadline_ = 0.0;
	if (editSession_.CompilePreview(context)) {
		Apply(context);
	}
}

bool Engine::ShaderGraphPreviewController::Apply(const EditorToolContext& context) {

	return scenePreview_.ApplyPreviewMaterial(
		context, editSession_.GetDraft().target, editSession_.GetPreviewMaterialID(), editSession_.GetStatusMessage());
}

void Engine::ShaderGraphPreviewController::Restore() {

	// Materialの復元に合わせて待機を解除する
	const bool applied = scenePreview_.IsMaterialApplied();
	scenePreview_.RestorePreviewMaterial();
	if (applied) {
		compileDeadline_ = 0.0;
	}
}

void Engine::ShaderGraphPreviewController::ResetTarget() {

	// 対象の切替へ古いコンパイル待機を持ち越さない
	scenePreview_.SetTargetEntityUUID({});
	compileDeadline_ = 0.0;
}
