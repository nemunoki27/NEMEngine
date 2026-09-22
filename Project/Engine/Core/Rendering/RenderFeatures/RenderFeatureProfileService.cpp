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

//============================================================================
//	RenderFeatureProfileService classMethods
//============================================================================
Engine::RenderFeatureProfileService&
Engine::RenderFeatureProfileService::GetInstance() {

	static RenderFeatureProfileService instance;
	return instance;
}

void Engine::RenderFeatureProfileService::EnsureLoaded() {

	if (!document_.loaded_) {
		Load();
	}
}

void Engine::RenderFeatureProfileService::Load() {

	document_.Read();
	RebuildRuntime();
	document_.dirty_ = false;
	document_.loaded_ = true;
}

void Engine::RenderFeatureProfileService::Reload() {

	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	document_.loaded_ = false;
	Load();
}

bool Engine::RenderFeatureProfileService::Save() const {

	return document_.Save();
}

void Engine::RenderFeatureProfileService::SetActiveProfileAsset(
	AssetID assetID, const AssetDatabase* assetDatabase) {

	SetActiveProfilePath(assetID && assetDatabase ?
		assetDatabase->ResolveFullPath(assetID) : std::filesystem::path{});
}

void Engine::RenderFeatureProfileService::SetActiveProfilePath(
	const std::filesystem::path& path) {

	const std::filesystem::path normalized = path.empty() ?
		std::filesystem::path{} : path.lexically_normal();
	if (document_.profilePath_ == normalized && document_.loaded_) {
		return;
	}
	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	document_.profilePath_ = normalized;
	document_.loaded_ = false;
	document_.dirty_ = false;
	Load();
}

void Engine::RenderFeatureProfileService::RebuildRuntime() {

	runtime_.Rebuild(document_.profile_);
	++runtimeGeneration_;
	if (runtimeGeneration_ == 0) {
		runtimeGeneration_ = 1;
	}
}

void Engine::RenderFeatureProfileService::CacheReflection(
	AssetID materialID, MaterialPassKind passKind,
	const std::vector<ShaderConstantBufferVariable>& variables,
	const std::vector<ShaderResourceBinding>& resources,
	const std::vector<ShaderResourceBinding>& samplers) {

	reflectionCache_.CacheReflection(materialID, passKind, variables, resources, samplers);
}

void Engine::RenderFeatureProfileService::ClearReflection(
	AssetID materialID) {

	reflectionCache_.ClearReflection(materialID);
}

void Engine::RenderFeatureProfileService::ClearReflectionCache() {

	reflectionCache_.ClearReflectionCache();
}

const Engine::RenderFeaturePassSettings*
Engine::RenderFeatureProfileService::FindPassByID(UUID passID) const {

	if (!passID) {
		return nullptr;
	}
	const auto found = std::find_if(document_.profile_.passes.begin(),
		document_.profile_.passes.end(), [passID](const RenderFeaturePassSettings& pass) {

		return pass.id == passID;
	});
	return found == document_.profile_.passes.end() ? nullptr : &*found;
}

const Engine::RenderFeaturePassSettings*
Engine::RenderFeatureProfileService::FindPassByName(
	std::string_view passName) const {

	if (passName.empty()) {
		return nullptr;
	}
	const auto found = std::find_if(document_.profile_.passes.begin(),
		document_.profile_.passes.end(), [passName](const RenderFeaturePassSettings& pass) {

		return pass.name == passName;
	});
	return found == document_.profile_.passes.end() ? nullptr : &*found;
}

const std::vector<Engine::ShaderConstantBufferVariable>*
Engine::RenderFeatureProfileService::FindReflectionVariables(
	AssetID materialID, MaterialPassKind passKind) const {

	return reflectionCache_.FindReflectionVariables(materialID, passKind);
}

const std::vector<Engine::ShaderResourceBinding>*
Engine::RenderFeatureProfileService::FindReflectionResources(
	AssetID materialID, MaterialPassKind passKind) const {

	return reflectionCache_.FindReflectionResources(materialID, passKind);
}

const std::vector<Engine::ShaderResourceBinding>*
Engine::RenderFeatureProfileService::FindReflectionSamplers(
	AssetID materialID, MaterialPassKind passKind) const {

	return reflectionCache_.FindReflectionSamplers(materialID, passKind);
}

//============================================================================
//	RenderFeatureProfileService classMethods
//============================================================================
