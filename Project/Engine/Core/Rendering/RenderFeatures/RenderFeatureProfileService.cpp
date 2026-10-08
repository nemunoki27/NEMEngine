#include "RenderFeatureProfileService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>

// c++
#include <algorithm>
#include <type_traits>
#include <utility>

//============================================================================
//	RenderFeatureProfileService classMethods
//============================================================================
Engine::RenderFeatureProfileService& Engine::RenderFeatureProfileService::GetInstance() {

	static RenderFeatureProfileService instance;
	return instance;
}

void Engine::RenderFeatureProfileService::EnsureLoaded() {

	if (!document_.loaded_) {
		Load();
	}
}

bool Engine::RenderFeatureProfileService::Load() {

	return ReadProfile(document_.profilePath_);
}

bool Engine::RenderFeatureProfileService::Reload() {

	return Load();
}

bool Engine::RenderFeatureProfileService::Save() const {

	return document_.Save();
}

bool Engine::RenderFeatureProfileService::SetActiveProfileAsset(AssetID assetID, const AssetDatabase* assetDatabase) {

	if (!assetID) {
		return SetActiveProfilePath({});
	}
	if (!assetDatabase) {
		return false;
	}
	const std::filesystem::path path = assetDatabase->ResolveFullPath(assetID);
	return !path.empty() && SetActiveProfilePath(path);
}

void Engine::RenderFeatureProfileService::SetRuntimeExtension(const RenderPassesAsset* extension, uint64_t revision) {

	const AssetID extensionID = extension ? extension->guid : AssetID{};
	if (runtimeExtensionID_ == extensionID && runtimeExtensionRevision_ == revision) {
		return;
	}
	// 完成した追加Passと入力世代をまとめて公開する
	RenderFeatureProfileRuntime runtime;
	runtime.Rebuild(extension ? ToRuntimeProfile(*extension) : RenderFeatureProfileAsset{});
	if (runtimeExtensionID_ != extensionID) {
		RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	}
	runtimeExtensionID_ = extensionID;
	runtimeExtensionRevision_ = revision;
	runtimeExtensionRuntime_ = std::move(runtime);
	++runtimeExtensionGeneration_;
	if (runtimeExtensionGeneration_ == 0) {
		runtimeExtensionGeneration_ = 1;
	}
}

bool Engine::RenderFeatureProfileService::SetActiveProfilePath(const std::filesystem::path& path) {

	const std::filesystem::path normalized = path.empty() ? std::filesystem::path{} : path.lexically_normal();
	if (document_.profilePath_ == normalized && document_.loaded_) {
		return true;
	}
	return ReadProfile(normalized);
}

bool Engine::RenderFeatureProfileService::ReadProfile(const std::filesystem::path& path) {

	// 編集用と実行用の両方が揃ってから公開する
	RenderFeatureProfileDocument candidate;
	if (!candidate.Read(path)) {
		return false;
	}
	RenderFeatureProfileRuntime runtime;
	runtime.Rebuild(candidate.profile_);
	candidate.loaded_ = true;
	static_assert(std::is_nothrow_move_assignable_v<RenderFeatureProfileDocument>);
	static_assert(std::is_nothrow_move_assignable_v<RenderFeatureProfileRuntime>);
	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	document_ = std::move(candidate);
	runtime_ = std::move(runtime);
	if (++runtimeGeneration_ == 0) {
		runtimeGeneration_ = 1;
	}
	return true;
}

void Engine::RenderFeatureProfileService::RebuildRuntime() {

	runtime_.Rebuild(document_.profile_);
	++runtimeGeneration_;
	if (runtimeGeneration_ == 0) {
		runtimeGeneration_ = 1;
	}
}

void Engine::RenderFeatureProfileService::CacheReflection(AssetID materialID, MaterialPassKind passKind,
	const std::vector<ShaderConstantBufferVariable>& variables, const std::vector<ShaderResourceBinding>& resources,
	const std::vector<ShaderResourceBinding>& samplers) {

	reflectionCache_.CacheReflection(materialID, passKind, variables, resources, samplers);
}

void Engine::RenderFeatureProfileService::ClearReflection(AssetID materialID) {

	reflectionCache_.ClearReflection(materialID);
}

void Engine::RenderFeatureProfileService::ClearReflectionCache() {

	reflectionCache_.ClearReflectionCache();
}

const Engine::RenderFeaturePassSettings* Engine::RenderFeatureProfileService::FindPassByID(UUID passID) const {

	if (!passID) {
		return nullptr;
	}
	const auto found = std::find_if(document_.profile_.passes.begin(), document_.profile_.passes.end(),
		[passID](const RenderFeaturePassSettings& pass) { return pass.id == passID; });
	return found == document_.profile_.passes.end() ? nullptr : &*found;
}

const Engine::RenderFeaturePassSettings* Engine::RenderFeatureProfileService::FindPassByName(std::string_view passName) const {

	if (passName.empty()) {
		return nullptr;
	}
	const auto found = std::find_if(document_.profile_.passes.begin(), document_.profile_.passes.end(),
		[passName](const RenderFeaturePassSettings& pass) { return pass.name == passName; });
	return found == document_.profile_.passes.end() ? nullptr : &*found;
}

const Engine::RenderFeaturePassSettings* Engine::RenderFeatureProfileService::FindRuntimeExtensionPassByID(UUID passID) const {

	if (!passID) {
		return nullptr;
	}
	const auto& passes = runtimeExtensionRuntime_.GetProfile().passes;
	const auto found = std::find_if(
		passes.begin(), passes.end(), [passID](const RenderFeaturePassSettings& pass) { return pass.id == passID; });
	return found == passes.end() ? nullptr : &*found;
}

const Engine::RenderFeaturePassSettings* Engine::RenderFeatureProfileService::FindRuntimeExtensionPassByName(
	std::string_view passName) const {

	if (passName.empty()) {
		return nullptr;
	}
	const auto& passes = runtimeExtensionRuntime_.GetProfile().passes;
	const auto found = std::find_if(
		passes.begin(), passes.end(), [passName](const RenderFeaturePassSettings& pass) { return pass.name == passName; });
	return found == passes.end() ? nullptr : &*found;
}

const std::vector<Engine::ShaderConstantBufferVariable>* Engine::RenderFeatureProfileService::FindReflectionVariables(
	AssetID materialID, MaterialPassKind passKind) const {

	return reflectionCache_.FindReflectionVariables(materialID, passKind);
}

const std::vector<Engine::ShaderResourceBinding>* Engine::RenderFeatureProfileService::FindReflectionResources(
	AssetID materialID, MaterialPassKind passKind) const {

	return reflectionCache_.FindReflectionResources(materialID, passKind);
}

const std::vector<Engine::ShaderResourceBinding>* Engine::RenderFeatureProfileService::FindReflectionSamplers(
	AssetID materialID, MaterialPassKind passKind) const {

	return reflectionCache_.FindReflectionSamplers(materialID, passKind);
}

//============================================================================
//	RenderFeatureProfileService classMethods
//============================================================================
