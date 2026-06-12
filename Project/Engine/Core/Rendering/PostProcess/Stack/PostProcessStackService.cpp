#include "PostProcessStackService.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackSerializer.h>

Engine::PostProcessStackService& Engine::PostProcessStackService::GetInstance() {

	static PostProcessStackService instance;
	return instance;
}

void Engine::PostProcessStackService::EnsureLoaded() {

	if (!loaded_) {
		Load();
	}
}

void Engine::PostProcessStackService::Load() {

	if (settingsPath_.empty()) {
		settings_ = PostProcessStackSettings{};
		RebuildRuntime();
		dirty_ = false;
		loaded_ = true;
		return;
	}

	settings_ = PostProcessStackSettings{};
	try {
		PostProcessStackSerializer::Load(settingsPath_, settings_);
	} catch (const std::exception& e) {
		Logger::Output(LogType::Engine,
			"[PostProcessStack] Failed to load settings: {} ({})", settingsPath_.string(), e.what());
		settings_ = PostProcessStackSettings{};
	} catch (...) {
		Logger::Output(LogType::Engine,
			"[PostProcessStack] Failed to load settings (unknown error): {}", settingsPath_.string());
		settings_ = PostProcessStackSettings{};
	}
	RebuildRuntime();
	dirty_ = false;
	loaded_ = true;
}

void Engine::PostProcessStackService::Save() const {

	if (settingsPath_.empty()) {
		return;
	}
	PostProcessStackSerializer::Save(settingsPath_, settings_);
}

void Engine::PostProcessStackService::Reload() {

	settings_ = PostProcessStackSettings{};
	try {
		PostProcessStackSerializer::Load(settingsPath_, settings_);
	} catch (const std::exception& e) {
		Logger::Output(LogType::Engine,
			"[PostProcessStack] Failed to reload settings: {} ({})", settingsPath_.string(), e.what());
		settings_ = PostProcessStackSettings{};
	} catch (...) {
		Logger::Output(LogType::Engine,
			"[PostProcessStack] Failed to reload settings (unknown error): {}", settingsPath_.string());
		settings_ = PostProcessStackSettings{};
	}
	RebuildRuntime();
	dirty_ = false;
}

void Engine::PostProcessStackService::SetActiveSettingsAsset(AssetID assetID, const AssetDatabase* assetDatabase) {

	if (!assetID || !assetDatabase) {
		SetActiveSettingsPath({});
		return;
	}

	const std::filesystem::path fullPath = assetDatabase->ResolveFullPath(assetID);
	SetActiveSettingsPath(fullPath);
}

void Engine::PostProcessStackService::SetActiveSettingsPath(const std::filesystem::path& settingsPath) {

	const std::filesystem::path nextPath = settingsPath.empty() ? std::filesystem::path{} : settingsPath.lexically_normal();
	if (settingsPath_ == nextPath && loaded_) {
		return;
	}

	settingsPath_ = nextPath;
	loaded_ = false;
	dirty_ = false;
	Load();
}

void Engine::PostProcessStackService::RebuildRuntime() {

	runtime_.passes.clear();
	runtime_.passes.reserve(settings_.passes.size());

	for (const auto& passSetting : settings_.passes) {

		PostProcessStackRuntimePass runtimePass{};
		runtimePass.id = passSetting.id;
		runtimePass.name = passSetting.name;
		runtimePass.enabled = passSetting.enabled;
		runtimePass.material = passSetting.materialGuid;
		runtimePass.passKind = passSetting.passKind;
		runtimePass.parameterOverrides = passSetting.parameterOverrides;
		runtimePass.textureGuids = passSetting.textureGuids;
		runtime_.passes.emplace_back(std::move(runtimePass));
	}
}

void Engine::PostProcessStackService::CacheReflection(AssetID materialId,
	const std::vector<ShaderConstantBufferVariable>& vars,
	const std::vector<ShaderResourceBinding>& srvBindings) {

	reflectionVars_[materialId] = vars;
	reflectionSRVs_[materialId] = srvBindings;
}

const std::vector<Engine::ShaderConstantBufferVariable>* Engine::PostProcessStackService::FindReflectionVars(AssetID materialId) const {

	auto it = reflectionVars_.find(materialId);
	if (it == reflectionVars_.end()) {
		return nullptr;
	}
	return &it->second;
}

const std::vector<ShaderResourceBinding>* PostProcessStackService::FindReflectionSRVs(AssetID materialId) const {

	auto it = reflectionSRVs_.find(materialId);
	if (it == reflectionSRVs_.end()) {
		return nullptr;
	}
	return &it->second;
}

void PostProcessStackService::ClearReflection(AssetID materialId) {

	reflectionVars_.erase(materialId);
	reflectionSRVs_.erase(materialId);
}

void Engine::PostProcessStackService::RequestShaderReload(AssetID materialId) {

	pendingReflectionReloads_.insert(materialId);
}

bool Engine::PostProcessStackService::TakeReloadRequest(AssetID materialId) {

	return pendingReflectionReloads_.erase(materialId) > 0;
}
