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
bool Engine::RenderFeatureProfileTool::IsPassMaterialSource(
	AssetType assetType, std::string_view assetPath) {

	return assetType == AssetType::Material ||
		(assetType == AssetType::Shader &&
			PostProcessAssetGenerator::IsComputeShaderSourcePath(assetPath));
}

Engine::AssetID Engine::RenderFeatureProfileTool::ResolvePassMaterial(
	const EditorToolContext& context, AssetID assetID,
	AssetType assetType, std::string_view assetPath) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	if (!database || !assetID) {
		statusMessage_ = "アセットを読み込めません";
		statusError_ = true;
		return {};
	}

	const AssetMeta* meta = database->Find(assetID);
	if (assetType == AssetType::Unknown && meta) {
		assetType = meta->type;
	}
	if (assetPath.empty() && meta) {
		assetPath = meta->assetPath;
	}
	if (assetType == AssetType::Material) {
		return assetID;
	}
	if (!IsPassMaterialSource(assetType, assetPath)) {
		statusMessage_ = "Materialまたは.cs.hlslを指定してください";
		statusError_ = true;
		return {};
	}

	const AssetID materialID = PostProcessAssetGenerator::EnsureUserAsset(
		database, std::string(assetPath));
	if (!materialID) {
		statusMessage_ = "Compute Shader用アセットを生成できません";
		statusError_ = true;
		return {};
	}
	statusMessage_ = "Compute Shader用アセットを生成しました";
	statusError_ = false;
	return materialID;
}

void Engine::RenderFeatureProfileTool::Tick(ToolContext& context) {

	if (requestedProfile_) {
		RenderFeatureProfileService::GetInstance().SetActiveProfileAsset(
			requestedProfile_, context.assetDatabase);
		observedProfile_ = requestedProfile_;
		requestedProfile_ = {};
		ClearSelection();
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
		ClearSelection();
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
		ClearSelection();
	}

	if (!observedProfile_ && !EnsureProfile(context)) {
		ImGui::TextDisabled("シーンまたは保存先を確認してください");
		ImGui::End();
		return;
	}

	AssetID importSource{};
	AssetEditSetting importSetting{};
	importSetting.allowDelete = false;
	if (MyGUI::AssetReferenceField("設定をインポート", importSource,
		context.toolContext.assetDatabase,
		{ AssetType::RenderFeatureProfile }, importSetting).valueChanged) {

		ImportProfileSettings(context, importSource);
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
		ClearSelection();
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

bool Engine::RenderFeatureProfileTool::ImportProfileSettings(
	const EditorToolContext& context, AssetID sourceProfile) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	if (!database || !observedProfile_ || !sourceProfile) {
		statusMessage_ = "取り込み元プロファイルを読み込めません";
		statusError_ = true;
		return false;
	}
	if (sourceProfile == observedProfile_) {
		statusMessage_ = "現在のプロファイルは取り込めません";
		statusError_ = true;
		return false;
	}

	const AssetMeta* sourceMeta = database->Find(sourceProfile);
	if (!sourceMeta || sourceMeta->type != AssetType::RenderFeatureProfile) {
		statusMessage_ = "Render Feature Profileを指定してください";
		statusError_ = true;
		return false;
	}

	RenderFeatureProfileAsset source{};
	const std::filesystem::path sourcePath =
		database->ResolveFullPath(sourceProfile);
	if (sourcePath.empty() ||
		!RenderFeatureProfileSerializer::Load(sourcePath, source)) {

		statusMessage_ = "取り込み元プロファイルを読み込めません";
		statusError_ = true;
		return false;
	}

	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	CopyRenderFeatureProfileSettings(service.GetProfile(), source);
	ClearSelection();
	SetDirty();
	statusMessage_ = "設定をインポートしました。保存してください";
	statusError_ = false;
	return true;
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
	profile.name = Algorithm::PathToUTF8(
		Algorithm::PathFromUTF8(assetPath).stem());
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
	if (MyGUI::CollapsingHeader("露出", false)) {
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

void Engine::RenderFeatureProfileTool::DrawPassDetail(
	const EditorToolContext& context) {

	RenderFeatureProfileAsset& profile =
		RenderFeatureProfileService::GetInstance().GetProfile();
	if (!selectedPass_) {
		if (selectedGroup_) {
			DrawSelectedGroupDetail(profile);
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
	changed |= DrawSelectedPassApplicationSettings(profile, editablePass);
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

				statusMessage_ = "SceneColor出力を変更できません。出力形式とサイズを確認してください";
				statusError_ = true;
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
			const AssetID materialID = ResolvePassMaterial(
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
		SetDirty();
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
		SetDirty();
	}
}

void Engine::RenderFeatureProfileTool::ClearSelection() {

	selectedPass_ = {};
	selectedGroup_ = {};
	selectedPasses_.clear();
}

void Engine::RenderFeatureProfileTool::SetDirty() {

	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	service.MarkDirty();
	service.RebuildRuntime();
}
