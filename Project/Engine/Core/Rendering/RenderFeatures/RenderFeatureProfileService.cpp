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

	if (!loaded_) {
		Load();
	}
}

void Engine::RenderFeatureProfileService::Load() {

	profile_ = RenderFeatureProfileAsset{};
	if (!profilePath_.empty() &&
		!RenderFeatureProfileSerializer::Load(profilePath_, profile_)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レンダー機能] プロファイルの読み込みに失敗しました path={}",
			profilePath_.string());
	}
	RebuildRuntime();
	dirty_ = false;
	loaded_ = true;
}

void Engine::RenderFeatureProfileService::Reload() {

	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	loaded_ = false;
	Load();
}

bool Engine::RenderFeatureProfileService::Save() const {

	if (profilePath_.empty()) {
		return false;
	}
	return RenderFeatureProfileSerializer::Save(profilePath_, profile_);
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
	if (profilePath_ == normalized && loaded_) {
		return;
	}
	RenderFeatureRuntimeOverrides::GetInstance().ResetAll();
	profilePath_ = normalized;
	loaded_ = false;
	dirty_ = false;
	Load();
}

void Engine::RenderFeatureProfileService::RebuildRuntime() {

	runtime_.Rebuild(profile_);
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

	const ReflectionKey key{ materialID, passKind };
	reflectionVariables_[key] = variables;
	reflectionResources_[key] = resources;
	reflectionSamplers_[key] = samplers;
}

void Engine::RenderFeatureProfileService::ClearReflection(
	AssetID materialID) {

	std::erase_if(reflectionVariables_,
		[materialID](const auto& entry) {

			return entry.first.material == materialID;
		});
	std::erase_if(reflectionResources_,
		[materialID](const auto& entry) {

			return entry.first.material == materialID;
		});
	std::erase_if(reflectionSamplers_,
		[materialID](const auto& entry) {

			return entry.first.material == materialID;
		});
}

void Engine::RenderFeatureProfileService::ClearReflectionCache() {

	reflectionVariables_.clear();
	reflectionResources_.clear();
	reflectionSamplers_.clear();
}

const Engine::RenderFeaturePassSettings*
Engine::RenderFeatureProfileService::FindPassByID(UUID passID) const {

	if (!passID) {
		return nullptr;
	}
	const auto found = std::find_if(profile_.passes.begin(),
		profile_.passes.end(), [passID](const RenderFeaturePassSettings& pass) {

		return pass.id == passID;
	});
	return found == profile_.passes.end() ? nullptr : &*found;
}

const Engine::RenderFeaturePassSettings*
Engine::RenderFeatureProfileService::FindPassByName(
	std::string_view passName) const {

	if (passName.empty()) {
		return nullptr;
	}
	const auto found = std::find_if(profile_.passes.begin(),
		profile_.passes.end(), [passName](const RenderFeaturePassSettings& pass) {

		return pass.name == passName;
	});
	return found == profile_.passes.end() ? nullptr : &*found;
}

const std::vector<Engine::ShaderConstantBufferVariable>*
Engine::RenderFeatureProfileService::FindReflectionVariables(
	AssetID materialID, MaterialPassKind passKind) const {

	const auto found = reflectionVariables_.find(
		ReflectionKey{ materialID, passKind });
	return found == reflectionVariables_.end() ? nullptr : &found->second;
}

const std::vector<Engine::ShaderResourceBinding>*
Engine::RenderFeatureProfileService::FindReflectionResources(
	AssetID materialID, MaterialPassKind passKind) const {

	const auto found = reflectionResources_.find(
		ReflectionKey{ materialID, passKind });
	return found == reflectionResources_.end() ? nullptr : &found->second;
}

const std::vector<Engine::ShaderResourceBinding>*
Engine::RenderFeatureProfileService::FindReflectionSamplers(
	AssetID materialID, MaterialPassKind passKind) const {

	const auto found = reflectionSamplers_.find(
		ReflectionKey{ materialID, passKind });
	return found == reflectionSamplers_.end() ? nullptr : &found->second;
}

//============================================================================
//	RenderFeatureProfileService classMethods
//============================================================================

namespace Engine {

	size_t RenderFeatureProfileService::ReflectionKeyHash::operator()(const ReflectionKey& key) const noexcept {

		return std::hash<AssetID>{}(key.material) ^
			(std::hash<uint8_t>{}(
				static_cast<uint8_t>(key.passKind)) << 1);
	}
}
