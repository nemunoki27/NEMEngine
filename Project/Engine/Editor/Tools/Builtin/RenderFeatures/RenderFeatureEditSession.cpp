#include "RenderFeatureEditSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <filesystem>
#include <utility>

//============================================================================
//	RenderFeatureEditSession classMethods
//============================================================================
bool Engine::RenderFeatureEditSession::IsPassMaterialSource(AssetType assetType, std::string_view assetPath) {

	return assetType == AssetType::Material ||
		   (assetType == AssetType::Shader && PostProcessAssetGenerator::IsComputeShaderSourcePath(assetPath));
}

Engine::AssetID Engine::RenderFeatureEditSession::ResolvePassMaterial(
	const EditorToolContext& context, AssetID assetID, AssetType assetType, std::string_view assetPath) {

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

	const AssetID materialID = PostProcessAssetGenerator::EnsureUserAsset(database, std::string(assetPath));
	if (!materialID) {
		statusMessage_ = "Compute Shader用アセットを生成できません";
		statusError_ = true;
		return {};
	}
	statusMessage_ = "Compute Shader用アセットを生成しました";
	statusError_ = false;
	return materialID;
}

void Engine::RenderFeatureEditSession::SetDirty() {

	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	service.MarkDirty();
	service.RebuildRuntime();
}

bool Engine::RenderFeatureEditSession::Tick(ToolContext& context) {

	if (requestedProfile_) {
		// 空のAssetも選択解除として反映する
		const AssetID requested = *requestedProfile_;
		requestedProfile_.reset();
		if (!RenderFeatureProfileService::GetInstance().SetActiveProfileAsset(requested, context.assetDatabase)) {
			SetStatusMessage("Render Passesを読み込めません", true);
			return false;
		}
		observedProfile_ = requested;
		SetStatusMessage("", false);
		return true;
	}
	return false;
}

void Engine::RenderFeatureEditSession::RequestProfile(AssetID assetID) {

	// 現在の選択へ戻したときは古い予約を取り消す
	requestedProfile_ = assetID == observedProfile_ ? std::nullopt : std::optional<AssetID>{assetID};
}

bool Engine::RenderFeatureEditSession::ImportProfileSettings(const EditorToolContext& context, AssetID sourceProfile) {

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
	if (!sourceMeta || sourceMeta->type != AssetType::RenderPasses) {
		statusMessage_ = "Render Passesを指定してください";
		statusError_ = true;
		return false;
	}

	RenderFeatureProfileAsset source{};
	const std::filesystem::path sourcePath = database->ResolveFullPath(sourceProfile);
	if (sourcePath.empty() || !RenderFeatureProfileSerializer::Load(sourcePath, source)) {

		statusMessage_ = "取り込み元プロファイルを読み込めません";
		statusError_ = true;
		return false;
	}
	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	CopyRenderFeatureProfileSettings(service.GetProfile(), source);
	SetDirty();
	statusMessage_ = "設定をインポートしました。保存してください";
	statusError_ = false;
	return true;
}

void Engine::RenderFeatureEditSession::SelectProfile(const EditorToolContext& context, AssetID profileAsset) {

	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	requestedProfile_.reset();
	if (!service.SetActiveProfileAsset(profileAsset, context.toolContext.assetDatabase)) {
		SetStatusMessage("Render Passesを読み込めません", true);
		return;
	}
	observedProfile_ = profileAsset;
	SetStatusMessage("", false);
}

void Engine::RenderFeatureEditSession::Save() {

	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	service.RebuildRuntime();
	statusError_ = !service.GetRuntime().GetDiagnostic().empty() || !service.Save();
	if (!statusError_) {
		service.ClearDirty();
	}
	statusMessage_ = statusError_ ? "保存できませんでした" : "保存しました";
}

void Engine::RenderFeatureEditSession::Reload() {

	statusError_ = !RenderFeatureProfileService::GetInstance().Reload();
	statusMessage_ = statusError_ ? "再読み込みできませんでした" : "再読み込みしました";
}

void Engine::RenderFeatureEditSession::SetStatusMessage(const std::string& message, bool error) {

	statusMessage_ = message;
	statusError_ = error;
}

void Engine::RenderFeatureEditSession::SynchronizePreview(const EditorToolContext& context) {

	if (!context.panelContext || !context.panelContext->renderPipeline) {
		return;
	}
	RenderAssetLibrary& library = context.panelContext->renderPipeline->GetRenderAssetLibrary();
	const AssetID assetID = GetProfileID();
	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	if (previewAsset_ && previewAsset_ != assetID) {
		// 切替前の編集を別のCameraへ残さない
		library.DiscardPreviewRenderPasses(previewAsset_);
		previewAsset_ = {};
		previewGeneration_ = 0;
	}
	if (!assetID || context.IsPlaying() ||
		(previewAsset_ == assetID && previewGeneration_ == service.GetRuntimeGeneration() &&
			library.HasPreviewRenderPasses(assetID))) {
		return;
	}
	// 未保存値を含む完成した構成を公開する
	const RenderFeatureProfileAsset& profile = service.GetProfile();
	RenderPassesAsset preview{};
	preview.guid = assetID;
	preview.name = profile.name;
	preview.passes = profile.passes;
	preview.hierarchy = profile.hierarchy;
	library.RegisterPreviewRenderPasses(std::move(preview));
	previewAsset_ = assetID;
	previewGeneration_ = service.GetRuntimeGeneration();
}
