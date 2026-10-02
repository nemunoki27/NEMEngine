#include "VolumeProfileTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorDrawerCommon.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

#include <imgui.h>

//============================================================================
//	VolumeProfileTool classMethods
//============================================================================
void Engine::VolumeProfileTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::VolumeProfileTool::OpenAsset(AssetID assetID) {

	if (assetID == assetID_) {
		openWindow_ = true;
		return;
	}
	if (dirty_) {
		pendingAsset_ = assetID;
	} else {
		requestedAsset_ = assetID;
	}
	openWindow_ = true;
}

void Engine::VolumeProfileTool::DrawEditorTool(
	const EditorToolContext& context) {

	if (requestedAsset_) {
		Load(context, requestedAsset_);
		if (context.IsPlaying()) {
			runtimeDraft_ = draft_;
		}
		requestedAsset_ = {};
	}
	if (context.IsPlaying() && !wasPlaying_) {
		runtimeDraft_ = draft_;
	}
	if (!context.IsPlaying() && wasPlaying_) {
		DiscardPreview(context);
	}
	wasPlaying_ = context.IsPlaying();
	if (!openWindow_) {
		return;
	}

	const bool wasOpen = openWindow_;
	if (!ImGui::Begin("Volume Profile", &openWindow_)) {
		if (wasOpen && !openWindow_ && dirty_) {
			openWindow_ = true;
			pendingClose_ = true;
		}
		DrawUnsavedChangesPopup(context);
		ImGui::End();
		return;
	}
	if (wasOpen && !openWindow_ && dirty_) {
		openWindow_ = true;
		pendingClose_ = true;
	}
	DrawUnsavedChangesPopup(context);

	AssetID selected = assetID_;
	AssetEditSetting setting{};
	if (MyGUI::AssetReferenceField("プロファイル", selected,
		context.toolContext.assetDatabase,
		{ AssetType::VolumeProfile }, setting).valueChanged) {
		OpenAsset(selected);
	}
	if (!assetID_) {
		ImGui::TextDisabled("ProjectからVolume Profileを選択してください");
		ImGui::End();
		return;
	}

	if (context.IsPlaying()) {
		ImGui::TextDisabled("Play中の変更は実行用にだけ反映され、Stop時に破棄されます");
		if (ImGui::Button("編集用へ適用")) {
			draft_ = runtimeDraft_;
			dirty_ = true;
			statusMessage_ = "編集用へ適用しました（未保存）";
		}
	} else {
		if (ImGui::Button("保存")) {
			Save(context);
		}
		ImGui::SameLine();
		if (ImGui::Button("再読み込み")) {
			DiscardPreview(context);
			Load(context, assetID_);
		}
	}
	if (!statusMessage_.empty()) {
		ImGui::TextUnformatted(statusMessage_.c_str());
	}
	DrawSettings(context, context.IsPlaying() ? runtimeDraft_ : draft_,
		!context.IsPlaying());
	ImGui::End();
}

bool Engine::VolumeProfileTool::Load(
	const EditorToolContext& context, AssetID assetID) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	const std::filesystem::path path = database ?
		database->ResolveFullPath(assetID) : std::filesystem::path{};
	VolumeProfileAsset loaded{};
	if (path.empty() || !FromJson(JsonAdapter::Load(path, true), loaded)) {
		statusMessage_ = "Volume Profileを読み込めません";
		return false;
	}
	loaded.guid = assetID;
	assetID_ = assetID;
	draft_ = std::move(loaded);
	dirty_ = false;
	statusMessage_.clear();
	return true;
}

bool Engine::VolumeProfileTool::Save(const EditorToolContext& context) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	const std::filesystem::path path = database ?
		database->ResolveFullPath(assetID_) : std::filesystem::path{};
	if (path.empty() || !JsonAdapter::SaveCanonical(path, ToJson(draft_))) {
		statusMessage_ = "Volume Profileを保存できません";
		return false;
	}
	dirty_ = false;
	statusMessage_ = "保存しました";
	return true;
}

void Engine::VolumeProfileTool::DiscardPreview(
	const EditorToolContext& context) {

	if (context.panelContext && context.panelContext->renderPipeline) {
		context.panelContext->renderPipeline->GetRenderAssetLibrary().
			InvalidateVolumeProfile(assetID_);
	}
}

void Engine::VolumeProfileTool::DrawUnsavedChangesPopup(
	const EditorToolContext& context) {

	if (!pendingClose_ && !pendingAsset_) {
		return;
	}
	ImGui::OpenPopup("Volume Profileの未保存編集");
	if (!ImGui::BeginPopupModal("Volume Profileの未保存編集", nullptr,
		ImGuiWindowFlags_AlwaysAutoResize)) {
		return;
	}
	ImGui::TextUnformatted("変更を保存しますか？");
	const auto finish = [&]() {
		if (pendingAsset_) {
			Load(context, pendingAsset_);
		}
		if (pendingClose_) {
			openWindow_ = false;
		}
		pendingAsset_ = {};
		pendingClose_ = false;
		ImGui::CloseCurrentPopup();
		};
	if (ImGui::Button("保存") && Save(context)) {
		finish();
	}
	ImGui::SameLine();
	if (ImGui::Button("破棄")) {
		DiscardPreview(context);
		dirty_ = false;
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

void Engine::VolumeProfileTool::DrawSettings(
	const EditorToolContext& context, VolumeProfileAsset& profile,
	bool persistent) {

	ColorPipelineSettings& settings = profile.colorPipeline;
	bool changed = false;
	changed |= MyGUI::EnumCombo("露出モード", settings.exposure.mode).valueChanged;
	changed |= MyGUI::DragFloat("EV100", settings.exposure.manualEV100).valueChanged;
	changed |= MyGUI::DragFloat("露出補正", settings.exposure.compensation).valueChanged;
	changed |= MyGUI::Checkbox("Pre-Exposure", settings.exposure.usePreExposure);
	changed |= MyGUI::DragFloat("最小EV100", settings.exposure.minEV100).valueChanged;
	changed |= MyGUI::DragFloat("最大EV100", settings.exposure.maxEV100).valueChanged;
	changed |= MyGUI::DragFloat("明順応速度", settings.exposure.speedUp).valueChanged;
	changed |= MyGUI::DragFloat("暗順応速度", settings.exposure.speedDown).valueChanged;
	changed |= MyGUI::DragFloat("Slope", settings.filmic.slope).valueChanged;
	changed |= MyGUI::DragFloat("Toe", settings.filmic.toe).valueChanged;
	changed |= MyGUI::DragFloat("Shoulder", settings.filmic.shoulder).valueChanged;
	changed |= MyGUI::ColorEdit("カラーフィルター",
		settings.colorGrading.colorFilter).valueChanged;
	changed |= MyGUI::DragFloat("色温度", settings.colorGrading.temperature).valueChanged;
	changed |= MyGUI::DragFloat("Tint", settings.colorGrading.tint).valueChanged;
	changed |= MyGUI::DragVector3("彩度", settings.colorGrading.saturation).valueChanged;
	changed |= MyGUI::DragVector3("コントラスト", settings.colorGrading.contrast).valueChanged;
	changed |= MyGUI::DragVector3("ガンマ", settings.colorGrading.gamma).valueChanged;
	changed |= MyGUI::DragVector3("ゲイン", settings.colorGrading.gain).valueChanged;
	changed |= MyGUI::DragVector3("オフセット", settings.colorGrading.offset).valueChanged;
	if (!changed) {
		return;
	}
	if (persistent) {
		dirty_ = true;
		statusMessage_ = "未保存";
	} else {
		statusMessage_ = "Play中の変更";
	}
	if (context.panelContext && context.panelContext->renderPipeline) {
		context.panelContext->renderPipeline->GetRenderAssetLibrary().
			RegisterPreviewVolumeProfile(profile);
	}
}
