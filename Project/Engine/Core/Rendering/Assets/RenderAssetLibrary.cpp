#include "RenderAssetLibrary.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/Shaders/ShaderCook.h>

// c++
#include <optional>
#include <system_error>

//============================================================================
//	RenderAssetLibrary templateMethods
//============================================================================
template <typename T>
const T* Engine::RenderAssetLibrary::LoadCachedAsset(std::unordered_map<AssetID, T>& cache, AssetID assetID) {

	if (!database_ || !assetID) {
		return nullptr;
	}

	// キャッシュデータを探す
	auto found = cache.find(assetID);
	if (found != cache.end()) {
		return &found->second;
	}

	// ファイルパスを検索
	const std::filesystem::path path = database_->ResolveFullPath(assetID);
	if (path.empty()) {
		return nullptr;
	}

	// データを読み込む
	nlohmann::json data = JsonAdapter::Load(path, true);
	T asset{};
	if (!FromJson(data, asset)) {
		return nullptr;
	}
	// 自身のGUIDが空ならアセットIDで補完する
	if (!asset.guid) {
		asset.guid = assetID;
	}
	ResolveRuntimeReferences(asset);
	// キャッシュに保存
	auto [it, inserted] = cache.emplace(assetID, std::move(asset));
	return &it->second;
}

//============================================================================
//	RenderAssetLibrary classMethods
//============================================================================
void Engine::RenderAssetLibrary::Init(AssetDatabase* database) {

	if (database_) {
		return;
	}

	database_ = database;
	Clear();
}

void Engine::RenderAssetLibrary::Clear() {

	shaderCache_.clear();
	pipelineCache_.clear();
	materialCache_.clear();
	fontCache_.clear();
	particleEffectCache_.clear();
}

void Engine::RenderAssetLibrary::ResolveRuntimeReferences(ShaderAsset& asset) {

	for (ShaderStageEntry& stage : asset.stages) {
		stage.ownerShader = asset.guid;

		std::filesystem::path sourcePath{};
		if (const std::optional<AssetID> sourceID = TryParseAssetGUID32Hex(stage.file)) {

			sourcePath = database_->ResolveFullPath(*sourceID);
		} else {

			sourcePath = database_->ResolveAssetPath(stage.file);
		}

		std::error_code ec;
		if (!sourcePath.empty() && std::filesystem::is_regular_file(sourcePath, ec)) {
			stage.file = Algorithm::PathToUTF8(sourcePath.lexically_normal());
		}
	}
}

void Engine::RenderAssetLibrary::ResolveRuntimeReferences(
	MaterialAsset& asset) {

	if (!asset.shaderGraph) {
		return;
	}
	// 製品では保存済みPassとCook済みShaderを使用し、Graphソースを要求しない
	if (ShaderCook::IsCookedProduct()) {
		return;
	}
	const std::filesystem::path graphPath =
		database_->ResolveFullPath(asset.shaderGraph);
	if (graphPath.empty()) {
		return;
	}

	ShaderGraphAsset graph{};
	if (!FromJson(JsonAdapter::Load(graphPath, true), graph)) {
		return;
	}
	ShaderGraphArtifact artifact{};
	if (!ShaderGraphArtifactCache::Compile(
		graph, asset.shaderGraph, artifact, database_)) {
		return;
	}
	RegisterDerivedShader(std::move(artifact.opaqueShader));
	RegisterDerivedShader(std::move(artifact.transparentShader));
	RegisterDerivedShader(std::move(artifact.depthShader));
	RegisterDerivedShader(std::move(artifact.pickingShader));
	RegisterDerivedShader(std::move(artifact.computeShader));
	RegisterDerivedPipeline(std::move(artifact.opaquePipeline));
	RegisterDerivedPipeline(std::move(artifact.transparentPipeline));
	RegisterDerivedPipeline(std::move(artifact.depthPipeline));
	RegisterDerivedPipeline(std::move(artifact.pickingPipeline));
	RegisterDerivedPipeline(std::move(artifact.computePipeline));
	ShaderGraphArtifactCache::ApplyToMaterial(artifact, asset);

	// Material側の上書きを維持しつつ新規公開値だけ補完する
	for (const ShaderGraphParameter& parameter : graph.parameters) {
		const MaterialParameterID parameterID =
			MaterialParameterID::FromUUID(parameter.id);
		if (!parameter.exposed ||
			parameter.scope == ShaderGraphParameterScope::Global ||
			asset.parameters.Find(parameterID)) {
			continue;
		}
		asset.parameters.Set(parameterID,
			parameter.name, parameter.semantic,
			parameter.defaultValue);
	}
}

const Engine::ShaderAsset* Engine::RenderAssetLibrary::LoadShader(AssetID assetID) {

	if (const ShaderAsset* shader = LoadCachedAsset(shaderCache_, assetID)) {
		return shader;
	}
	ShaderAsset cooked{};
	if (!ShaderCook::LoadShaderAsset(assetID, cooked)) {
		return nullptr;
	}
	auto [found, inserted] = shaderCache_.emplace(assetID, std::move(cooked));
	return &found->second;
}

const Engine::RenderPipelineAsset* Engine::RenderAssetLibrary::LoadPipeline(AssetID assetID) {

	if (const RenderPipelineAsset* pipeline =
		LoadCachedAsset(pipelineCache_, assetID)) {
		return pipeline;
	}
	RenderPipelineAsset cooked{};
	if (!ShaderCook::LoadPipelineAsset(assetID, cooked)) {
		return nullptr;
	}
	auto [found, inserted] = pipelineCache_.emplace(
		assetID, std::move(cooked));
	return &found->second;
}

const Engine::MaterialAsset* Engine::RenderAssetLibrary::LoadMaterial(AssetID assetID) {

	return LoadCachedAsset(materialCache_, assetID);
}

const Engine::MSDFFontAsset* Engine::RenderAssetLibrary::LoadFont(AssetID assetID) {

	return LoadCachedAsset(fontCache_, assetID);
}

const Engine::ParticleEffectAsset* Engine::RenderAssetLibrary::LoadParticleEffect(AssetID assetID) {

	return LoadCachedAsset(particleEffectCache_, assetID);
}

void Engine::RenderAssetLibrary::RegisterDerivedShader(
	ShaderAsset shader) {

	if (shader.guid) {
		shaderCache_.insert_or_assign(
			shader.guid, std::move(shader));
	}
}

void Engine::RenderAssetLibrary::RegisterDerivedPipeline(
	RenderPipelineAsset pipeline) {

	if (pipeline.guid) {
		pipelineCache_.insert_or_assign(
			pipeline.guid, std::move(pipeline));
	}
}

void Engine::RenderAssetLibrary::RegisterDerivedMaterial(
	MaterialAsset material) {

	if (material.guid) {
		materialCache_.insert_or_assign(
			material.guid, std::move(material));
	}
}
