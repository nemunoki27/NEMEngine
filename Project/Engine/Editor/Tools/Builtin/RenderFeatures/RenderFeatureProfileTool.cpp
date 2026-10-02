#include "RenderFeatureProfileTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>

// c++
#include <algorithm>
#include <filesystem>

#include <imgui.h>

//============================================================================
//	RenderFeatureProfileTool classMethods
//============================================================================

void Engine::RenderFeatureProfileTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::RenderFeatureProfileTool::OpenAsset(AssetID assetID) {

	RequestAssetSwitch(assetID);
	openWindow_ = true;
}

void Engine::RenderFeatureProfileTool::DrawEditorTool(const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::RenderFeatureProfileTool::DrawWindow(const EditorToolContext& context) {

	const bool wasOpen = openWindow_;
	if (!ImGui::Begin("Render Extension", &openWindow_)) {
		if (wasOpen && !openWindow_ &&
			RenderFeatureProfileService::GetInstance().IsDirty()) {
			openWindow_ = true;
			pendingClose_ = true;
		}
		DrawUnsavedChangesPopup(context);
		ImGui::End();
		return;
	}
	if (wasOpen && !openWindow_ &&
		RenderFeatureProfileService::GetInstance().IsDirty()) {
		openWindow_ = true;
		pendingClose_ = true;
	}
	DrawUnsavedChangesPopup(context);

	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	service.EnsureLoaded();
	AssetID profileAsset = editSession_.GetProfileID();
	AssetEditSetting assetSetting{};
	if (MyGUI::AssetReferenceField("プロファイル", profileAsset,
		context.toolContext.assetDatabase,
		{ AssetType::RenderExtension }, assetSetting).valueChanged) {

		RequestAssetSwitch(profileAsset);
	}

	if (!editSession_.GetProfileID()) {
		ImGui::TextDisabled("ProjectからRender Extensionを選択してください");
		ImGui::End();
		return;
	}
	if (context.IsPlaying()) {
		ImGui::TextDisabled("Play中の変更は実行用にだけ反映され、Stop時に破棄されます");
		RenderFeatureRuntimeOverrides& overrides =
			RenderFeatureRuntimeOverrides::GetInstance();
		const RenderFeatureProfileAsset& runtimeProfile =
			service.GetRuntimeExtension().GetProfile();
		for (const RenderFeaturePassSettings& pass : runtimeProfile.passes) {
			ImGui::PushID(static_cast<int32_t>(pass.id.value));
			bool enabled = overrides.IsEnabled(pass.id, pass.enabled);
			if (ImGui::Checkbox("##Enabled", &enabled)) {
				overrides.SetEnabled(pass.id, enabled);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(pass.name.c_str());
			ImGui::SameLine();
			bool sceneColorOutput = overrides.IsSceneColorOutput(
				pass.id, pass.sceneColorOutput);
			if (ImGui::Checkbox("Scene Colorへ出力", &sceneColorOutput)) {
				overrides.SetSceneColorOutput(
					runtimeProfile, pass.id, sceneColorOutput);
			}
			ImGui::PopID();
		}
		ImGui::End();
		return;
	}

	AssetID importSource{};
	AssetEditSetting importSetting{};
	importSetting.allowDelete = false;
	if (MyGUI::AssetReferenceField("設定をインポート", importSource,
		context.toolContext.assetDatabase,
		{ AssetType::RenderExtension }, importSetting).valueChanged) {

		ImportProfileSettings(context, importSource);
	}

	const float buttonWidth = ImGui::GetContentRegionAvail().x * 0.5f - 2.0f;
	if (ImGui::Button("保存", ImVec2(buttonWidth, 0.0f))) {
		editSession_.Save();
	}
	ImGui::SameLine();
	if (ImGui::Button("再読み込み", ImVec2(buttonWidth, 0.0f))) {
		editSession_.Reload();
		ClearSelection();
	}
	if (!editSession_.GetStatusMessage().empty()) {
		ImGui::TextColored(editSession_.HasError() ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) :
			ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "%s", editSession_.GetStatusMessage().c_str());
	}
	if (!service.GetRuntime().GetDiagnostic().empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s",
			service.GetRuntime().GetDiagnostic().c_str());
	}

	const float listWidth = (std::max)(220.0f,
		ImGui::GetContentRegionAvail().x * 0.28f);
	if (ImGui::BeginChild("RenderFeaturePassList", ImVec2(listWidth, 0.0f),
		ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
		DrawPassList(context);
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

void Engine::RenderFeatureProfileTool::DrawUnsavedChangesPopup(
	const EditorToolContext& context) {

	if (!pendingClose_ && !pendingAsset_) {
		return;
	}
	ImGui::OpenPopup("Render Extensionの未保存編集");
	if (!ImGui::BeginPopupModal("Render Extensionの未保存編集", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}
	ImGui::TextUnformatted("変更を保存しますか？");
	const auto finish = [&]() {
		if (pendingAsset_) {
			editSession_.SelectProfile(context, pendingAsset_);
			ClearSelection();
		}
		if (pendingClose_) {
			openWindow_ = false;
		}
		pendingAsset_ = {};
		pendingClose_ = false;
		ImGui::CloseCurrentPopup();
		};
	if (ImGui::Button("保存")) {
		editSession_.Save();
		if (!editSession_.HasError()) {
			finish();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("破棄")) {
		editSession_.Reload();
		finish();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル")) {
		pendingAsset_ = {};
		pendingClose_ = false;
		openWindow_ = true;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void Engine::RenderFeatureProfileTool::RequestAssetSwitch(AssetID assetID) {

	if (assetID == editSession_.GetProfileID()) {
		return;
	}
	if (RenderFeatureProfileService::GetInstance().IsDirty()) {
		pendingAsset_ = assetID;
		return;
	}
	editSession_.RequestProfile(assetID);
}

void Engine::RenderFeatureProfileTool::DrawPassDetail(const EditorToolContext& context) {

	RenderFeatureProfileAsset& profile =
		RenderFeatureProfileService::GetInstance().GetProfile();
	if (!selectedPass_) {
		if (selectedGroup_) {
			DrawSelectedGroupDetail(context, profile);
			return;
		}
		ImGui::TextDisabled("編集するパスを選択してください");
		return;
	}
	if (!DrawSelectedPassControls(profile)) {
		return;
	}
	const auto selected = std::find_if(profile.passes.begin(),
		profile.passes.end(), [&](const RenderFeaturePassSettings& pass) {

			return pass.id == selectedPass_;
		});
	if (selected == profile.passes.end()) {
		ClearSelection();
		return;
	}

	bool changed = false;
	RenderFeaturePassSettings& editablePass = *selected;
	changed |= MyGUI::InputText("名前", editablePass.name).valueChanged;
	changed |= MyGUI::EnumCombo("種類", editablePass.type).valueChanged;
	changed |= MyGUI::EnumCombo("実行位置", editablePass.anchor).valueChanged;
	if (editablePass.anchor == RenderFeatureAnchor::AfterToneMap) {
		ImGui::TextDisabled("トーンマッピング後、ScreenUIの前にビューへ描画します");
	}
	changed |= DrawSelectedPassApplicationSettings(context, profile, editablePass);
	changed |= MyGUI::Checkbox("Game View", editablePass.gameView);
	changed |= MyGUI::Checkbox("Scene View", editablePass.sceneView);
	// Play中の出力切り替えはスクリプトと同じ実行時設定を使う
	RenderFeatureRuntimeOverrides& overrides = RenderFeatureRuntimeOverrides::GetInstance();
	bool sceneColorOutput = context.IsPlaying() ?
		overrides.IsSceneColorOutput(editablePass.id, editablePass.sceneColorOutput) :
		editablePass.sceneColorOutput;
	if (MyGUI::Checkbox("Scene Colorへ出力", sceneColorOutput)) {

		if (context.IsPlaying()) {

			if (!overrides.SetSceneColorOutput(
				RenderFeatureProfileService::GetInstance().GetRuntime().GetProfile(),
				editablePass.id, sceneColorOutput)) {

				editSession_.SetStatusMessage("SceneColor出力を変更できません。出力形式とサイズを確認してください", true);
			}
		} else {

			editablePass.sceneColorOutput = sceneColorOutput;
			changed = true;
			if (sceneColorOutput) {

				for (RenderFeaturePassSettings& candidate : profile.passes) {

					if (candidate.id != editablePass.id && candidate.anchor == editablePass.anchor) {

						candidate.sceneColorOutput = false;
					}
				}
			}
		}
	}

	AssetEditSetting setting{};
	AssetID selectedAsset = editablePass.material;
	if (MyGUI::AssetReferenceField("マテリアル", selectedAsset,
		context.toolContext.assetDatabase,
		{ AssetType::Material, AssetType::Shader }, setting).valueChanged) {

		if (!selectedAsset) {
			editablePass.material = {};
			changed = true;
		} else {
			const AssetMeta* meta = context.toolContext.assetDatabase ?
				context.toolContext.assetDatabase->Find(selectedAsset) : nullptr;
			const AssetType assetType = meta ? meta->type : AssetType::Unknown;
			const std::string_view assetPath = meta ?
				std::string_view(meta->assetPath) : std::string_view{};
			const AssetID materialID = editSession_.ResolvePassMaterial(
				context, selectedAsset, assetType, assetPath);
			if (materialID) {
				editablePass.material = materialID;
				if (assetType == AssetType::Shader) {
					editablePass.type = RenderFeaturePassType::Compute;
					editablePass.materialPass = MaterialPassKind::PostProcess;
				}
				changed = true;
			}
		}
	}
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
	static const std::vector<std::string> sourceItems{
		"直前のパス", "Scene Color", "指定パス" };
	std::string selectedSource = sourceKindLabel(editablePass.sourceKind);
	if (MyGUI::StringCombo("主入力", selectedSource,
		sourceItems).valueChanged) {

		if (selectedSource == sourceItems[0]) {
			editablePass.sourceKind = RenderFeatureSourceKind::PreviousPass;
		} else if (selectedSource == sourceItems[1]) {
			editablePass.sourceKind = RenderFeatureSourceKind::SceneColor;
		} else {
			editablePass.sourceKind = RenderFeatureSourceKind::PassOutput;
		}
		if (editablePass.sourceKind != RenderFeatureSourceKind::PassOutput) {
			editablePass.source = {};
		}
		changed = true;
	}
	if (editablePass.sourceKind == RenderFeatureSourceKind::PassOutput) {
		changed |= DrawOutputReferenceCombo("参照出力", profile, editablePass,
			editablePass.source, "未設定");
	}

	if (changed) {
		editSession_.SetDirty();
	}
	ImGui::Separator();
	DrawOutputs(editablePass);
	DrawResources(context, editablePass);

	bool gpuChanged = false;
	if (MyGUI::CollapsingHeader("GPU品質", false)) {
		MyGUI::ScopedPropertyLabelWidth width("RenderFeatureGPUQuality");
		gpuChanged |= MyGUI::Checkbox("動的解像度",
			editablePass.adaptiveResolution);
		gpuChanged |= MyGUI::DragFloat("GPU予算(ms)", editablePass.gpuBudgetMs,
			{ .minValue = 0.1f, .maxValue = 33.0f }).valueChanged;
		gpuChanged |= MyGUI::DragFloat("最小スケール",
			editablePass.minResolutionScale,
			{ .minValue = 0.25f, .maxValue = 1.0f }).valueChanged;
		gpuChanged |= MyGUI::DragFloat("最大スケール",
			editablePass.maxResolutionScale,
			{ .minValue = 0.25f, .maxValue = 1.0f }).valueChanged;
		gpuChanged |= MyGUI::DragFloat("調整幅", editablePass.resolutionStep,
			{ .minValue = 0.05f, .maxValue = 0.5f }).valueChanged;
		int32_t interval = static_cast<int32_t>(
			editablePass.adjustmentIntervalFrames);
		if (MyGUI::DragInt("調整間隔", interval,
			{ .minValue = 1, .maxValue = 240 }).valueChanged) {

			editablePass.adjustmentIntervalFrames =
				static_cast<uint32_t>(interval);
			gpuChanged = true;
		}
	}
	if (gpuChanged) {
		editSession_.SetDirty();
	}
}

void Engine::RenderFeatureProfileTool::ClearSelection() {

	selectedPass_ = {};
	selectedGroup_ = {};
	selectedPasses_.clear();
}

void Engine::RenderFeatureProfileTool::Tick(ToolContext& context) {

	if (editSession_.Tick(context)) {
		ClearSelection();
	}
}

bool Engine::RenderFeatureProfileTool::ImportProfileSettings(const EditorToolContext& context, AssetID sourceProfile) {

	if (!editSession_.ImportProfileSettings(context, sourceProfile)) {
		return false;
	}
	ClearSelection();
	return true;
}

bool Engine::RenderFeatureProfileTool::EnsureProfile(const EditorToolContext& context) {

	if (!editSession_.CanCreateProfile(context)) {
		return false;
	}
	if (!ImGui::Button("現在のシーン用プロファイルを作成",
		ImVec2(ImGui::GetContentRegionAvail().x, 0.0f))) {
		return false;
	}
	return editSession_.CreateProfile(context);
}
