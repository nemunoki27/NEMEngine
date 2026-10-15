#include "RenderFeatureProfileTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Rendering/PostProcess/Bloom/BloomSettings.h>

// c++
#include <algorithm>

// imgui
#include <imgui.h>

//============================================================================
//	RenderFeatureProfileTool classMethods
//============================================================================

void Engine::RenderFeatureProfileTool::OpenEditorTool() {

	openWindow_ = true;
	requestCameraProfile_ = true;
}

void Engine::RenderFeatureProfileTool::OpenAsset(AssetID assetID) {

	// Projectから開いたAssetはCameraの設定へ切り替えない
	requestCameraProfile_ = false;
	RequestAssetSwitch(assetID);
	openWindow_ = true;
}

bool Engine::RenderFeatureProfileTool::HasPendingEdits() const {

	return RenderFeatureProfileService::GetInstance().IsDirty();
}

void Engine::RenderFeatureProfileTool::RequestResolvePendingEdits() {

	// Editor終了時も同じ確認画面を開く
	resolvePendingEdits_ = true;
	closeResult_ = EditorToolCloseResult::None;
	pendingClose_ = true;
	openWindow_ = true;
}

Engine::EditorToolCloseResult Engine::RenderFeatureProfileTool::ConsumePendingEditCloseResult() {

	const EditorToolCloseResult result = closeResult_;
	closeResult_ = EditorToolCloseResult::None;
	return result;
}

void Engine::RenderFeatureProfileTool::DrawEditorTool(const EditorToolContext& context) {

	if (requestCameraProfile_ && context.panelContext && context.panelContext->renderPipeline) {
		// メニューから開くときは描画中のCameraに合わせる
		const auto& view = context.panelContext->renderPipeline->GetResolvedView(RenderViewKind::Game);
		if (const auto* camera = view.FindOutputCamera()) {
			RequestAssetSwitch(camera->renderPasses);
		}
		requestCameraProfile_ = false;
	}
	if (openWindow_) {
		DrawWindow(context);
	}
	editSession_.SynchronizePreview(context);
}

void Engine::RenderFeatureProfileTool::DrawWindow(const EditorToolContext& context) {

	const bool wasOpen = openWindow_;
	if (!ImGui::Begin("Render Passes", &openWindow_)) {
		if (wasOpen && !openWindow_ && RenderFeatureProfileService::GetInstance().IsDirty()) {
			openWindow_ = true;
			pendingClose_ = true;
		}
		DrawUnsavedChangesPopup(context);
		ImGui::End();
		return;
	}
	if (wasOpen && !openWindow_ && RenderFeatureProfileService::GetInstance().IsDirty()) {
		openWindow_ = true;
		pendingClose_ = true;
	}
	DrawUnsavedChangesPopup(context);

	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	service.EnsureLoaded();
	AssetID profileAsset = editSession_.GetProfileID();
	AssetEditSetting assetSetting{};
	if (MyGUI::AssetReferenceField(
			"プロファイル", profileAsset, context.toolContext.assetDatabase, {AssetType::RenderPasses}, assetSetting)
			.valueChanged) {

		RequestAssetSwitch(profileAsset);
	}

	if (context.panelContext && context.panelContext->renderPipeline) {
		// 編集対象とCameraの参照先を区別する
		const auto& view = context.panelContext->renderPipeline->GetResolvedView(RenderViewKind::Game);
		const auto* camera = view.FindOutputCamera();
		const AssetID cameraProfile = camera ? camera->renderPasses : AssetID{};
		AssetDatabase* database = context.toolContext.assetDatabase;
		const AssetMeta* meta = database ? database->Find(cameraProfile) : nullptr;
		MyGUI::Text("GameViewの設定", meta ? meta->assetPath : cameraProfile ? ToString(cameraProfile) : "未設定");
		if (editSession_.GetProfileID() && editSession_.GetProfileID() != cameraProfile) {
			ImGui::TextWrapped("この設定はGameViewの主Cameraに使用されていません");
			if (cameraProfile && ImGui::Button("Cameraで使用中の設定を開く")) {
				RequestAssetSwitch(cameraProfile);
			}
		}
		if (camera && !camera->postProcessEnabled) {
			ImGui::TextDisabled("Cameraのポストプロセス設定は無効です");
		}
	}

	if (!editSession_.GetProfileID()) {
		ImGui::TextDisabled("ProjectからRender Passesを選択してください");
		ImGui::End();
		return;
	}
	if (context.IsPlaying()) {
		ImGui::TextDisabled("Play中の変更は実行用にだけ反映され、Stop時に破棄されます");
		RenderFeatureRuntimeOverrides& overrides = RenderFeatureRuntimeOverrides::GetInstance();
		const RenderFeatureProfileAsset& runtimeProfile = service.GetProfile();
		bool gameView = overrides.IsViewEnabled(editSession_.GetProfileID(), false, runtimeProfile.gameView);
		bool sceneView = overrides.IsViewEnabled(editSession_.GetProfileID(), true, runtimeProfile.sceneView);
		bool viewChanged = MyGUI::Checkbox("GameView出力", gameView);
		viewChanged |= MyGUI::Checkbox("SceneView出力", sceneView);
		if (viewChanged) overrides.SetViewEnabled(editSession_.GetProfileID(), gameView, sceneView);
		for (const RenderFeaturePassSettings& pass : runtimeProfile.passes) {
			ImGui::PushID(static_cast<int32_t>(pass.id.value));
			bool enabled = overrides.IsEnabled(pass.id, pass.enabled);
			if (ImGui::Checkbox("##Enabled", &enabled)) {
				overrides.SetEnabled(pass.id, enabled);
			}
			ImGui::SameLine();
			ImGui::TextUnformatted(pass.name.c_str());
			if (pass.type == RenderFeaturePassType::Bloom) {
				MaterialParameterSet parameters = pass.parameterOverrides;
				DrawBloomSettings(context, pass, parameters);
			}
			ImGui::PopID();
		}
		ImGui::End();
		return;
	}

	AssetID importSource{};
	AssetEditSetting importSetting{};
	importSetting.allowDelete = false;
	if (MyGUI::AssetReferenceField(
			"設定をインポート", importSource, context.toolContext.assetDatabase, {AssetType::RenderPasses}, importSetting)
			.valueChanged) {

		ImportProfileSettings(context, importSource);
	}

	// 全パスの出力先をまとめて切り替える
	bool viewChanged = MyGUI::Checkbox("GameView出力", service.GetProfile().gameView);
	viewChanged |= MyGUI::Checkbox("SceneView出力", service.GetProfile().sceneView);
	if (viewChanged) editSession_.SetDirty();

	const float buttonWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("保存", ImVec2(buttonWidth, 0.0f))) {
		editSession_.Save();
	}
	ImGui::SameLine();
	if (ImGui::Button("再読み込み", ImVec2(buttonWidth, 0.0f))) {
		editSession_.Reload();
		ClearSelection();
	}
	if (!editSession_.GetStatusMessage().empty()) {
		ImGui::TextColored(editSession_.HasError() ? ImVec4(1.0f, 0.35f, 0.35f, 1.0f) : ImVec4(0.45f, 0.9f, 0.55f, 1.0f), "%s",
			editSession_.GetStatusMessage().c_str());
	}
	if (!service.GetRuntime().GetDiagnostic().empty()) {
		ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", service.GetRuntime().GetDiagnostic().c_str());
	}

	const float listWidth = (std::max)(220.0f, ImGui::GetContentRegionAvail().x * 0.28f);
	if (ImGui::BeginChild(
			"RenderFeaturePassList", ImVec2(listWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
		DrawPassList(context);
	}
	ImGui::EndChild();
	ImGui::SameLine();
	if (ImGui::BeginChild("RenderFeaturePassDetail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
		DrawPassDetail(context);
	}
	ImGui::EndChild();
	ImGui::End();
}

void Engine::RenderFeatureProfileTool::DrawUnsavedChangesPopup(const EditorToolContext& context) {

	if (!pendingClose_ && !pendingAsset_) {
		return;
	}
	ImGui::OpenPopup("Render Passesの未保存編集");
	if (!MyGUI::BeginPopupModal("Render Passesの未保存編集", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}
	ImGui::TextWrapped("%s", "変更を保存しますか？");
	const auto finish = [&]() {
		if (pendingAsset_) {
			editSession_.SelectProfile(context, *pendingAsset_);
			if (editSession_.HasError()) {
				return;
			}
			ClearSelection();
		}
		if (pendingClose_) {
			openWindow_ = false;
		}
		pendingAsset_.reset();
		pendingClose_ = false;
		if (resolvePendingEdits_) {
			closeResult_ = EditorToolCloseResult::Accepted;
			resolvePendingEdits_ = false;
		}
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
		if (!editSession_.HasError()) {
			finish();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル")) {
		pendingAsset_.reset();
		pendingClose_ = false;
		openWindow_ = true;
		if (resolvePendingEdits_) {
			closeResult_ = EditorToolCloseResult::Cancelled;
			resolvePendingEdits_ = false;
		}
		ImGui::CloseCurrentPopup();
	}
	if (editSession_.HasError()) {
		ImGui::TextWrapped("%s", editSession_.GetStatusMessage().c_str());
	}
	ImGui::EndPopup();
}

void Engine::RenderFeatureProfileTool::RequestAssetSwitch(AssetID assetID) {

	if (assetID == editSession_.GetProfileID()) {
		pendingAsset_.reset();
		editSession_.RequestProfile(assetID);
		return;
	}
	if (RenderFeatureProfileService::GetInstance().IsDirty()) {
		pendingAsset_ = assetID;
		return;
	}
	editSession_.RequestProfile(assetID);
}

void Engine::RenderFeatureProfileTool::DrawPassDetail(const EditorToolContext& context) {

	RenderFeatureProfileAsset& profile = RenderFeatureProfileService::GetInstance().GetProfile();
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
	const auto selected = std::find_if(profile.passes.begin(), profile.passes.end(),
		[&](const RenderFeaturePassSettings& pass) { return pass.id == selectedPass_; });
	if (selected == profile.passes.end()) {
		ClearSelection();
		return;
	}

	bool changed = false;
	RenderFeaturePassSettings& editablePass = *selected;
	changed |= MyGUI::InputText("名前", editablePass.name).valueChanged;
	if (MyGUI::EnumCombo("種類", editablePass.type).valueChanged) {
		changed = true;
		if (editablePass.type == RenderFeaturePassType::Bloom) {
			// Bloomの入出力は内部で一組として管理する
			editablePass.material = {};
			editablePass.materialPass = MaterialPassKind::PostProcess;
			editablePass.anchor = RenderFeatureAnchor::AfterTransparent;
			editablePass.outputs = {RenderFeatureOutputSettings{}};
			editablePass.adaptiveResolution = false;
			editablePass.parameterOverrides.clear();
			editablePass.passInputs.clear();
			editablePass.sceneInputs.clear();
			editablePass.textureOverrides.clear();
			editablePass.samplerOverrides.clear();
			WriteBloomSettings(editablePass.parameterOverrides, BloomSettings{});
		}
	}
	const bool bloom = editablePass.type == RenderFeaturePassType::Bloom;
	changed |= bloom ? DrawBloomAnchor(editablePass.anchor) : MyGUI::EnumCombo("実行位置", editablePass.anchor).valueChanged;
	if (editablePass.anchor == RenderFeatureAnchor::AfterToneMap) {
		ImGui::TextDisabled("トーンマッピング後、ScreenUIの前にビューへ描画します");
	}
	changed |= DrawSelectedPassApplicationSettings(context, profile, editablePass);

	if (!bloom) {
		AssetEditSetting setting{};
		AssetID selectedAsset = editablePass.material;
		if (MyGUI::AssetReferenceField(
				"マテリアル", selectedAsset, context.toolContext.assetDatabase, {AssetType::Material, AssetType::Shader}, setting)
				.valueChanged) {

			if (!selectedAsset) {
				editablePass.material = {};
				changed = true;
			} else {
				const AssetMeta* meta =
					context.toolContext.assetDatabase ? context.toolContext.assetDatabase->Find(selectedAsset) : nullptr;
				const AssetType assetType = meta ? meta->type : AssetType::Unknown;
				const std::string_view assetPath = meta ? std::string_view(meta->assetPath) : std::string_view{};
				const AssetID materialID = editSession_.ResolvePassMaterial(context, selectedAsset, assetType, assetPath);
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
		changed |= MyGUI::EnumCombo("マテリアルパス", editablePass.materialPass).valueChanged;
		if (editablePass.type == RenderFeaturePassType::RayTracing) {
			int32_t rayGeneration = static_cast<int32_t>(editablePass.rayGenerationIndex);
			if (MyGUI::DragInt("Ray Generation", rayGeneration, {.minValue = 0}).valueChanged) {

				editablePass.rayGenerationIndex = static_cast<uint32_t>(rayGeneration);
				changed = true;
			}
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
	static const std::vector<std::string> sourceItems{"直前のパス", "Scene Color", "指定パス"};
	std::string selectedSource = sourceKindLabel(editablePass.sourceKind);
	if (MyGUI::StringCombo("主入力", selectedSource, sourceItems).valueChanged) {

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
		changed |= DrawOutputReferenceCombo("参照出力", profile, editablePass, editablePass.source, "未設定");
	}

	if (changed) {
		editSession_.SetDirty();
	}
	ImGui::Separator();
	if (bloom) {
		DrawBloomSettings(context, editablePass, editablePass.parameterOverrides);
		return;
	}
	DrawOutputs(editablePass);
	DrawResources(context, editablePass);

	bool gpuChanged = false;
	if (MyGUI::CollapsingHeader("GPU品質", false)) {
		MyGUI::ScopedPropertyLabelWidth width("RenderFeatureGPUQuality");
		gpuChanged |= MyGUI::Checkbox("動的解像度", editablePass.adaptiveResolution);
		gpuChanged |=
			MyGUI::DragFloat("GPU予算(ms)", editablePass.gpuBudgetMs, {.minValue = 0.1f, .maxValue = 33.0f}).valueChanged;
		gpuChanged |= MyGUI::DragFloat("最小スケール", editablePass.minResolutionScale, {.minValue = 0.25f, .maxValue = 1.0f})
						  .valueChanged;
		gpuChanged |= MyGUI::DragFloat("最大スケール", editablePass.maxResolutionScale, {.minValue = 0.25f, .maxValue = 1.0f})
						  .valueChanged;
		gpuChanged |=
			MyGUI::DragFloat("調整幅", editablePass.resolutionStep, {.minValue = 0.05f, .maxValue = 0.5f}).valueChanged;
		int32_t interval = static_cast<int32_t>(editablePass.adjustmentIntervalFrames);
		if (MyGUI::DragInt("調整間隔", interval, {.minValue = 1, .maxValue = 240}).valueChanged) {

			editablePass.adjustmentIntervalFrames = static_cast<uint32_t>(interval);
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
