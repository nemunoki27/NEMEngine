#include "FBXScenePreparation.h"

//============================================================================
//	include
//============================================================================
#include "FBXDocumentReferences.h"

// assimp
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

const aiScene* Engine::FBXScenePreparation::Prepare(
	Assimp::Importer& importer, const aiScene* scene, const std::filesystem::path& path) {

	if (!scene || !FBXDocumentReferences::IsDocumentPath(path)) {
		return scene;
	}
	// 骨の配置とアニメーションはSkinning側で評価する
	for (uint32_t index = 0; index < scene->mNumMeshes; ++index) {
		if (scene->mMeshes[index]->HasBones()) {
			return scene;
		}
	}
	// SubMesh名と部品の分割を残してノード変換を焼き込む
	importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);
	return importer.ApplyPostProcessing(aiProcess_PreTransformVertices);
}
