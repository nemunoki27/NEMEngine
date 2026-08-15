#include "RenderFeatureProfileTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

// imgui
#include <imgui.h>

// c++
#include <algorithm>
#include <filesystem>

namespace {

	Engine::SceneHeader* ResolveActiveSceneHeader(
		const Engine::ToolContext& context) {

		if (context.sceneInstances && context.activeSceneInstanceID) {
			Engine::SceneInstance* scene = context.sceneInstances->Find(
				context.activeSceneInstanceID);
			if (scene) {
				return &scene->header;
			}
		}
		return const_cast<Engine::SceneHeader*>(context.activeSceneHeader);
	}

}

//============================================================================
//	RenderFeatureProfileTool classMethods
//============================================================================
void Engine::RenderFeatureProfileTool::Tick(ToolContext& context) {

	if (requestedProfile_) {
		RenderFeatureProfileService::GetInstance().SetActiveProfileAsset(
			requestedProfile_, context.assetDatabase);
		observedProfile_ = requestedProfile_;
		requestedProfile_ = {};
		selectedPassIndex_ = -1;
		return;
	}
	if (!context.activeSceneHeader) {
		return;
	}
	const AssetID profile = context.activeSceneHeader->renderFeatureProfile;
	if (profile == observedProfile_) {
		return;
	}
	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	if (!service.IsDirty()) {
		service.SetActiveProfileAsset(profile, context.assetDatabase);
		observedProfile_ = profile;
		selectedPassIndex_ = -1;
	}
}

void Engine::RenderFeatureProfileTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::RenderFeatureProfileTool::OpenAsset(AssetID assetID) {

	requestedProfile_ = assetID;
	openWindow_ = true;
}

void Engine::RenderFeatureProfileTool::DrawEditorTool(
	const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::RenderFeatureProfileTool::DrawWindow(
	const EditorToolContext& context) {

	if (!ImGui::Begin("レンダー機能設定", &openWindow_)) {
		ImGui::End();
		return;
	}

	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	service.EnsureLoaded();
	AssetID profileAsset = observedProfile_;
	AssetEditSetting assetSetting{};
	if (MyGUI::AssetReferenceField("プロファイル", profileAsset,
		context.toolContext.assetDatabase,
		{ AssetType::RenderFeatureProfile }, assetSetting).valueChanged) {

		SceneHeader* header = ResolveActiveSceneHeader(context.toolContext);
		if (header) {
			header->renderFeatureProfile = profileAsset;
		}
		service.SetActiveProfileAsset(
			profileAsset, context.toolContext.assetDatabase);
		observedProfile_ = profileAsset;
		selectedPassIndex_ = -1;
	}

	if (!observedProfile_ && !EnsureProfile(context)) {
		ImGui::TextDisabled("シーンまたは保存先を確認してください");
		ImGui::End();
		return;
	}

	const float buttonWidth = ImGui::GetContentRegionAvail().x * 0.5f - 2.0f;
	if (ImGui::Button("保存", ImVec2(buttonWidth, 0.0f))) {
		service.RebuildRuntime();
		statusError_ = !service.GetRuntime().GetDiagnostic().empty() ||
			!service.Save();
		if (!statusError_) {
			service.ClearDirty();
		}
		statusMessage_ = statusError_ ?
			"保存できませんでした" : "保存しました";
	}
	ImGui::SameLine();
	if (ImGui::Button("再読み込み", ImVec2(buttonWidth, 0.0f))) {
		service.Reload();
		selectedPassIndex_ = -1;
		statusMessage_ = "再読み込みしました";
		statusError_ = false;
	}
	if (!statusMessage_.empty()) {
		ImGui::TextColored(statusError_ ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) :
			ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "%s", statusMessage_.c_str());
	}
	if (!service.GetRuntime().GetDiagnostic().empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s",
			service.GetRuntime().GetDiagnostic().c_str());
	}

	DrawColorPipeline();
	ImGui::Separator();
	const float listWidth = (std::max)(220.0f,
		ImGui::GetContentRegionAvail().x * 0.28f);
	if (ImGui::BeginChild("RenderFeaturePassList", ImVec2(listWidth, 0.0f),
		ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
		DrawPassList();
	}
	ImGui::EndChild();
	ImGui::SameLine();
	if (ImGui::BeginChild("RenderFeaturePassDetail", ImVec2(0.0f, 0.0f),
		ImGuiChildFlags_Borders)) {
		DrawPassDetail(context);
	}
	ImGui::EndChild();
	ImGui::End();
}

bool Engine::RenderFeatureProfileTool::EnsureProfile(
	const EditorToolContext& context) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	SceneHeader* header = ResolveActiveSceneHeader(context.toolContext);
	if (!database || !header || context.toolContext.activeScenePath.empty()) {
		return false;
	}
	if (!ImGui::Button("現在のシーン用プロファイルを作成",
		ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		return false;
	}

	const std::string assetPath = MakeDefaultRenderFeatureProfilePath(
		std::string(context.toolContext.activeScenePath));
	const std::filesystem::path fullPath = database->ResolveAssetPath(assetPath);
	std::error_code ec;
	std::filesystem::create_directories(fullPath.parent_path(), ec);
	RenderFeatureProfileAsset profile{};
	profile.name = std::filesystem::path(assetPath).stem().string();
	if (!RenderFeatureProfileSerializer::Save(fullPath, profile)) {
		statusMessage_ = "プロファイル作成に失敗しました";
		statusError_ = true;
		return false;
	}
	header->renderFeatureProfile = database->ImportOrGet(
		assetPath, AssetType::RenderFeatureProfile);
	observedProfile_ = header->renderFeatureProfile;
	RenderFeatureProfileService::GetInstance().SetActiveProfileAsset(
		observedProfile_, database);
	return true;
}

void Engine::RenderFeatureProfileTool::DrawColorPipeline() {

	if (!MyGUI::CollapsingHeader("カラー出力", false)) {
		return;
	}
	ColorPipelineSettings& settings =
		RenderFeatureProfileService::GetInstance().GetProfile().colorPipeline;
	bool changed = false;
	ImGui::Indent();
	if (MyGUI::CollapsingHeader("露出", true)) {
		MyGUI::ScopedPropertyLabelWidth width("RenderFeatureExposure");
		changed |= MyGUI::EnumCombo("露出モード", settings.exposure.mode).valueChanged;
		changed |= MyGUI::DragFloat("EV100", settings.exposure.manualEV100).valueChanged;
		changed |= MyGUI::DragFloat("露出補正", settings.exposure.compensation).valueChanged;
		changed |= MyGUI::Checkbox("Pre-Exposure", settings.exposure.usePreExposure);
	}
	if (MyGUI::CollapsingHeader("フィルミック", false)) {
		MyGUI::ScopedPropertyLabelWidth width("RenderFeatureFilmic");
		changed |= MyGUI::DragFloat("Slope", settings.filmic.slope).valueChanged;
		changed |= MyGUI::DragFloat("Toe", settings.filmic.toe).valueChanged;
		changed |= MyGUI::DragFloat("Shoulder", settings.filmic.shoulder).valueChanged;
	}
	if (MyGUI::CollapsingHeader("カラーグレーディング", false)) {
		MyGUI::ScopedPropertyLabelWidth width("RenderFeatureColorGrading");
		changed |= MyGUI::ColorEdit("フィルター",
			settings.colorGrading.colorFilter).valueChanged;
		changed |= MyGUI::DragFloat("色温度",
			settings.colorGrading.temperature).valueChanged;
		changed |= MyGUI::DragFloat("Tint", settings.colorGrading.tint).valueChanged;
		changed |= MyGUI::DragVector3("彩度",
			settings.colorGrading.saturation).valueChanged;
	}
	ImGui::Unindent();
	if (changed) {
		SetDirty();
	}
}

void Engine::RenderFeatureProfileTool::DrawPassList() {

	RenderFeatureProfileAsset& profile =
		RenderFeatureProfileService::GetInstance().GetProfile();
	if (ImGui::Button("追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		ImGui::OpenPopup("RenderFeatureAddPass");
	}
	if (ImGui::BeginPopup("RenderFeatureAddPass")) {
		for (const auto [type, label] : {
			std::pair{ RenderFeaturePassType::Compute, "Compute" },
			std::pair{ RenderFeaturePassType::RayTracing, "DispatchRays" } }) {

			if (!ImGui::MenuItem(label)) {
				continue;
			}
			RenderFeaturePassSettings pass{};
			pass.id = UUID::New();
			pass.name = label;
			pass.type = type;
			pass.materialPass = type == RenderFeaturePassType::Compute ?
				MaterialPassKind::PostProcess : MaterialPassKind::RayTracing;
			pass.outputs.emplace_back();
			profile.passes.emplace_back(std::move(pass));
			selectedPassIndex_ = static_cast<int32_t>(profile.passes.size()) - 1;
			SetDirty();
		}
		ImGui::EndPopup();
	}

	for (int32_t index = 0;
		index < static_cast<int32_t>(profile.passes.size()); ++index) {

		RenderFeaturePassSettings& pass = profile.passes[index];
		ImGui::PushID(index);
		bool enabled = pass.enabled;
		if (ImGui::Checkbox("##Enabled", &enabled)) {
			pass.enabled = enabled;
			SetDirty();
		}
		ImGui::SameLine();
		if (ImGui::Selectable(pass.name.c_str(), selectedPassIndex_ == index)) {
			selectedPassIndex_ = index;
		}
		ImGui::PopID();
	}
}

void Engine::RenderFeatureProfileTool::DrawPassDetail(
	const EditorToolContext& context) {

	RenderFeatureProfileAsset& profile =
		RenderFeatureProfileService::GetInstance().GetProfile();
	if (selectedPassIndex_ < 0 ||
		selectedPassIndex_ >= static_cast<int32_t>(profile.passes.size())) {

		ImGui::TextDisabled("編集するパスを選択してください");
		return;
	}
	bool changed = false;
	const float orderButtonWidth =
		ImGui::GetContentRegionAvail().x * 0.5f - 2.0f;
	ImGui::BeginDisabled(selectedPassIndex_ == 0);
	if (ImGui::Button("上へ", ImVec2(orderButtonWidth, 0.0f))) {
		std::swap(profile.passes[selectedPassIndex_],
			profile.passes[selectedPassIndex_ - 1]);
		--selectedPassIndex_;
		changed = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(selectedPassIndex_ + 1 >=
		static_cast<int32_t>(profile.passes.size()));
	if (ImGui::Button("下へ", ImVec2(orderButtonWidth, 0.0f))) {
		std::swap(profile.passes[selectedPassIndex_],
			profile.passes[selectedPassIndex_ + 1]);
		++selectedPassIndex_;
		changed = true;
	}
	ImGui::EndDisabled();
	RenderFeaturePassSettings& selectedPass =
		profile.passes[selectedPassIndex_];
	RenderFeaturePassSettings& editablePass = selectedPass;
	changed |= MyGUI::InputText("名前", editablePass.name).valueChanged;
	changed |= MyGUI::EnumCombo("種類", editablePass.type).valueChanged;
	changed |= MyGUI::EnumCombo("実行位置", editablePass.anchor).valueChanged;
	changed |= MyGUI::Checkbox("Game View", editablePass.gameView);
	changed |= MyGUI::Checkbox("Scene View", editablePass.sceneView);
	changed |= MyGUI::Checkbox("Scene Colorへ出力",
		editablePass.sceneColorOutput);
	changed |= InspectorDrawerCommon::DrawLayerMaskField(
		"対象レイヤー", editablePass.targetMask).valueChanged;
	if (editablePass.sceneColorOutput) {
		for (RenderFeaturePassSettings& candidate : profile.passes) {
			if (candidate.id != editablePass.id &&
				candidate.anchor == editablePass.anchor) {
				candidate.sceneColorOutput = false;
			}
		}
	}

	AssetEditSetting setting{};
	changed |= MyGUI::AssetReferenceField("マテリアル", editablePass.material,
		context.toolContext.assetDatabase,
		{ AssetType::Material }, setting).valueChanged;
	changed |= MyGUI::EnumCombo("マテリアルパス",
		editablePass.materialPass).valueChanged;
	if (editablePass.type == RenderFeaturePassType::RayTracing) {
		int32_t rayGeneration =
			static_cast<int32_t>(editablePass.rayGenerationIndex);
		if (MyGUI::DragInt("Ray Generation", rayGeneration,
			{ .minValue = 0 }).valueChanged) {

			editablePass.rayGenerationIndex =
				static_cast<uint32_t>(rayGeneration);
			changed = true;
		}
	}
	if (MyGUI::CollapsingHeader("GPU品質", false)) {
		MyGUI::ScopedPropertyLabelWidth width("RenderFeatureGPUQuality");
		changed |= MyGUI::Checkbox("動的解像度", editablePass.adaptiveResolution);
		changed |= MyGUI::DragFloat("GPU予算(ms)", editablePass.gpuBudgetMs,
			{ .minValue = 0.1f, .maxValue = 33.0f }).valueChanged;
		changed |= MyGUI::DragFloat("最小スケール", editablePass.minResolutionScale,
			{ .minValue = 0.25f, .maxValue = 1.0f }).valueChanged;
		changed |= MyGUI::DragFloat("最大スケール", editablePass.maxResolutionScale,
			{ .minValue = 0.25f, .maxValue = 1.0f }).valueChanged;
		changed |= MyGUI::DragFloat("調整幅", editablePass.resolutionStep,
			{ .minValue = 0.05f, .maxValue = 0.5f }).valueChanged;
		int32_t interval = static_cast<int32_t>(
			editablePass.adjustmentIntervalFrames);
		if (MyGUI::DragInt("調整間隔", interval,
			{ .minValue = 1, .maxValue = 240 }).valueChanged) {

			editablePass.adjustmentIntervalFrames =
				static_cast<uint32_t>(interval);
			changed = true;
		}
	}

	const auto sourceKindLabel = [](RenderFeatureSourceKind kind) {

		switch (kind) {
		case RenderFeatureSourceKind::PreviousPass:
			return "直前のパス";
		case RenderFeatureSourceKind::SceneColor:
			return "Scene Color";
		case RenderFeatureSourceKind::PassOutput:
			return "指定パス";
		}
		return "不明";
	};
	if (ImGui::BeginCombo("主入力",
		sourceKindLabel(editablePass.sourceKind))) {

		for (const RenderFeatureSourceKind kind : {
			RenderFeatureSourceKind::PreviousPass,
			RenderFeatureSourceKind::SceneColor,
			RenderFeatureSourceKind::PassOutput }) {

			const bool selected = editablePass.sourceKind == kind;
			if (ImGui::Selectable(sourceKindLabel(kind), selected)) {
				editablePass.sourceKind = kind;
				if (kind != RenderFeatureSourceKind::PassOutput) {
					editablePass.source = {};
				}
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	if (editablePass.sourceKind == RenderFeatureSourceKind::PassOutput) {
		changed |= DrawOutputReferenceCombo("参照出力", profile, editablePass,
			editablePass.source, "未設定");
	}

	if (changed) {
		SetDirty();
	}
	ImGui::Separator();
	DrawOutputs(editablePass);
	DrawResources(context, editablePass);

	if (ImGui::Button("パスを削除",
		ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		profile.passes.erase(profile.passes.begin() + selectedPassIndex_);
		selectedPassIndex_ = -1;
		SetDirty();
	}
}

void Engine::RenderFeatureProfileTool::SetDirty() {

	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	service.MarkDirty();
	service.RebuildRuntime();
}
