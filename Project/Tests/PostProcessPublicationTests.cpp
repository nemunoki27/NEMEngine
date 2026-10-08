#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetPublication.h>

// c++
#include <algorithm>
#include <array>

bool NEMTests::TestPostProcessPublication() {

	using namespace Engine;
	TestDirectory directory("PostProcessPublication", RuntimePaths::GetGameAssetsRoot() / "Shaders");
	std::array<PostProcessAssetSource, 2> sources;
	for (size_t index = 0; index < sources.size(); ++index) {
		auto& source = sources[index];
		source.name = index == 0 ? "Invert" : "Vignette";
		source.builtin = true;
		source.sourceShader = RuntimePaths::ToAssetPath(directory.GetPath() / (source.name + ".CS.hlsl"));
		source.shaderFile = source.sourceShader;
		source.shaderPath = RuntimePaths::ToAssetPath(directory.GetPath() / (source.name + ".shader.json"));
		source.pipelinePath = RuntimePaths::ToAssetPath(directory.GetPath() / (source.name + ".pipeline.json"));
		source.materialPath = RuntimePaths::ToAssetPath(directory.GetPath() / (source.name + ".material.json"));
		if (!StorageFileUtility::WriteBytes(
				RuntimePaths::ResolveAssetPath(source.sourceShader), "[numthreads(1,1,1)] void main() {}")) {
			return false;
		}
	}
	const auto shaderPath = RuntimePaths::ResolveAssetPath(sources[0].shaderPath);
	const auto target = RuntimePaths::ResolveAssetPath(sources[1].materialPath);
	const nlohmann::json custom{{"name", "CustomShader"}, {"stages", nlohmann::json::array()}};
	const nlohmann::json previous{
		{"name", "PreviousMaterial"}, {"generated", true}, {"generatedBy", "PostProcessAssetGenerator"}, {"domain", "Compute"}};
	if (!JsonFile::Save(shaderPath, custom) || !JsonFile::Save(target, previous)) {
		return false;
	}
	AssetDatabase database;
	if (!database.Init()) {
		return false;
	}
	const auto originalShader = database.ImportOrGet(sources[0].shaderPath, AssetType::Shader);
	const auto originalMaterial = database.ImportOrGet(sources[1].materialPath, AssetType::Material);
	const auto structure = database.GetStructureRevision();
	const auto content = database.GetContentRevision();
	const auto shaderRevision = StorageFileUtility::FileRevision(shaderPath);
	const auto* retainedShader = database.Find(originalShader);
	std::vector<AssetID> identifiers{{9, 9}};
	std::string diagnostic;
	{
		// 後半の保存失敗で前半の文書と索引も戻す
		TestFileReadLock lock(target);
		if (PostProcessAssetPublication::PublishBatch(database, sources, identifiers, diagnostic) || diagnostic.empty() ||
			identifiers != std::vector<AssetID>{{9, 9}} || database.Find(originalShader) != retainedShader ||
			database.GetStructureRevision() != structure || database.GetContentRevision() != content ||
			JsonFile::Load(target, false) != previous || StorageFileUtility::FileRevision(shaderPath) != shaderRevision ||
			std::filesystem::exists(RuntimePaths::ResolveAssetPath(sources[0].pipelinePath)) ||
			std::filesystem::exists(RuntimePaths::ResolveAssetPath(sources[0].materialPath))) {
			return false;
		}
	}
	// ロック解除後は同じ操作を再試行できる
	if (!PostProcessAssetPublication::PublishBatch(database, sources, identifiers, diagnostic) || identifiers.size() != 2 ||
		!diagnostic.empty() || identifiers[1] != originalMaterial ||
		StorageFileUtility::FileRevision(shaderPath) != shaderRevision || JsonFile::Load(shaderPath, false) != custom) {
		return false;
	}
	for (size_t index = 0; index < sources.size(); ++index) {
		const auto* pipeline = database.FindByPath(sources[index].pipelinePath);
		const auto* shader = database.FindByPath(sources[index].shaderPath);
		const auto* material = database.Find(identifiers[index]);
		if (!pipeline || !shader || !material || material->type != AssetType::Material) {
			return false;
		}
		const auto& dependencies = database.FindDependencies(material->guid);
		const auto& pipelineDependencies = database.FindDependencies(pipeline->guid);
		if (std::find(dependencies.begin(), dependencies.end(), pipeline->guid) == dependencies.end() ||
			std::find(pipelineDependencies.begin(), pipelineDependencies.end(), shader->guid) == pipelineDependencies.end()) {
			return false;
		}
	}
	// 標準値と既存の手書きShader参照を維持
	const auto first = JsonFile::Load(RuntimePaths::ResolveAssetPath(sources[0].materialPath), false);
	const auto second = JsonFile::Load(target, false);
	if (first.at("parameters").at("strength") != 1.0f || second.at("parameters").at("intensity") != 0.45f ||
		database.FindByPath(sources[0].shaderPath)->guid != originalShader) {
		return false;
	}
	const auto existing = PostProcessAssetGenerator::EnsureUserAsset(&database, sources[0].sourceShader);
	return existing == identifiers[0];
}
