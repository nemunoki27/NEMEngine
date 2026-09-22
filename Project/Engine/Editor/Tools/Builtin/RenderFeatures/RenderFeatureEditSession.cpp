#include "RenderFeatureEditSession.h"
#include <Engine/Editor/Tools/Core/IEditorTool.h>

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
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

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

bool Engine::RenderFeatureEditSession::IsPassMaterialSource(AssetType assetType, std::string_view assetPath) {

	return assetType == AssetType::Material ||
		(assetType == AssetType::Shader &&
			PostProcessAssetGenerator::IsComputeShaderSourcePath(assetPath));
}

Engine::AssetID Engine::RenderFeatureEditSession::ResolvePassMaterial(
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

void Engine::RenderFeatureEditSession::SetDirty() {

	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	service.MarkDirty();
	service.RebuildRuntime();
}

bool Engine::RenderFeatureEditSession::Tick(ToolContext& context) {

	if (requestedProfile_) {
		RenderFeatureProfileService::GetInstance().SetActiveProfileAsset(
			requestedProfile_, context.assetDatabase);
		observedProfile_ = requestedProfile_;
		requestedProfile_ = {};
		return true;
	}
	if (!context.activeSceneHeader) {
		return false;
	}
	const AssetID profile = context.activeSceneHeader->renderFeatureProfile;
	if (profile == observedProfile_) {
		return false;
	}
	RenderFeatureProfileService& service =
		RenderFeatureProfileService::GetInstance();
	if (!service.IsDirty()) {
		service.SetActiveProfileAsset(profile, context.assetDatabase);
		observedProfile_ = profile;
		return true;
	}
	return false;
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
	SetDirty();
	statusMessage_ = "設定をインポートしました。保存してください";
	statusError_ = false;
	return true;
}

bool Engine::RenderFeatureEditSession::CreateProfile(const EditorToolContext& context) {

	AssetDatabase* database = context.toolContext.assetDatabase;
	SceneHeader* header = ResolveActiveSceneHeader(context.toolContext);
	if (!database || !header || context.toolContext.activeScenePath.empty()) {
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

void Engine::RenderFeatureEditSession::SelectProfile(const EditorToolContext& context, AssetID profileAsset) {

	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	SceneHeader* header = ResolveActiveSceneHeader(context.toolContext);
	if (header) {
		header->renderFeatureProfile = profileAsset;
	}
	service.SetActiveProfileAsset(
		profileAsset, context.toolContext.assetDatabase);
	observedProfile_ = profileAsset;
}

void Engine::RenderFeatureEditSession::Save() {

	RenderFeatureProfileService& service = RenderFeatureProfileService::GetInstance();
	service.RebuildRuntime();
	statusError_ = !service.GetRuntime().GetDiagnostic().empty() ||
		!service.Save();
	if (!statusError_) {
		service.ClearDirty();
	}
	statusMessage_ = statusError_ ?
		"保存できませんでした" : "保存しました";
}

void Engine::RenderFeatureEditSession::Reload() {

	RenderFeatureProfileService::GetInstance().Reload();
	statusMessage_ = "再読み込みしました";
	statusError_ = false;
}

bool Engine::RenderFeatureEditSession::CanCreateProfile(const EditorToolContext& context) const {

	return context.toolContext.assetDatabase && ResolveActiveSceneHeader(context.toolContext) &&
		!context.toolContext.activeScenePath.empty();
}

void Engine::RenderFeatureEditSession::SetStatusMessage(const std::string& message, bool error) {

	statusMessage_ = message;
	statusError_ = error;
}
