#include "ModelFileDependencyCollector.h"

//============================================================================
//	include
//============================================================================
#include "AssimpMaterialTextureExtractor.h"
#include "ModelFileIOSystem.h"
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>

// c++
#include <algorithm>
#include <utility>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

void Engine::ModelFileDependencyCollector::ModelDependencies::AddFile(const std::filesystem::path& path) {

	const auto normalized = std::filesystem::absolute(path).lexically_normal();
	if (std::find(files.begin(), files.end(), normalized) == files.end()) {
		files.push_back(normalized);
	}
}

bool Engine::ModelFileDependencyCollector::Collect(
	const std::filesystem::path& modelPath, ModelDependencies& dependencies, std::string& diagnostic) {

	dependencies = {};
	diagnostic.clear();
	// OBJ・glTF・FBXの文書解析を通常読込と共有
	Assimp::Importer importer;
	auto* fileSystem = new ModelFileIOSystem(modelPath, &dependencies);
	importer.SetIOHandler(fileSystem);
	const aiScene* scene = importer.ReadFile(fileSystem->GetModelPath(), aiProcess_Triangulate);
	if (!scene) {
		diagnostic = importer.GetErrorString();
		return false;
	}

	// 画像の名前補完も通常描画と同じ結果を使う
	TextureAssetResolver resolver;
	resolver.Build(modelPath);
	dependencies.materialTextures.reserve(scene->mNumMaterials);
	for (uint32_t index = 0; index < scene->mNumMaterials; ++index) {
		auto textures = AssimpMaterialTextureExtractor::CollectResolvedFiles(scene->mMaterials[index], resolver);
		for (const auto& path : textures) {
			dependencies.AddFile(path);
		}
		dependencies.materialTextures.push_back(std::move(textures));
	}
	return true;
}

bool Engine::ModelFileDependencyCollector::Collect(
	const std::filesystem::path& modelPath, std::vector<std::filesystem::path>& dependencies, std::string& diagnostic) {

	// 読込失敗時も実際に開いたファイルを呼出し元へ返す
	dependencies.clear();
	ModelDependencies collected;
	const bool success = Collect(modelPath, collected, diagnostic);
	dependencies = std::move(collected.files);
	return success;
}

bool Engine::ModelFileDependencyCollector::CollectMaterialTextures(const std::filesystem::path& modelPath,
	std::vector<std::vector<std::filesystem::path>>& textures, std::string& diagnostic) {

	ModelDependencies collected;
	if (!Collect(modelPath, collected, diagnostic)) {
		return false;
	}
	// 画像一覧は読込成功時だけ差し替える
	textures = std::move(collected.materialTextures);
	return true;
}
