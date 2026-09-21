#include "ModelPreviewUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Rendering/Meshes/Import/AssimpMaterialTextureExtractor.h>

// c++
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <filesystem>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

//============================================================================
//	ModelPreviewUtility functions
//============================================================================
namespace {

	// モデル読み込み時に使うassimpの後処理フラグ、プレビュー用に法線や接線も生成する
	constexpr uint32_t kModelPreviewAssimpFlags =
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_SortByPType;

	// assimpの座標をエンジンの左手座標へ変換する
	Engine::Vector3 ToEnginePreviewPosition(const aiVector3D& pos) {

		return Engine::Vector3(-pos.x, pos.y, pos.z);
	}
}

void Engine::ModelPreviewUtility::CollectNodePositions(const aiScene* scene, const aiNode* node,
	std::vector<Vector3>& outPositions) {

	if (!scene || !node) {
		return;
	}

	for (uint32_t meshRefIndex = 0; meshRefIndex < node->mNumMeshes; ++meshRefIndex) {

		const uint32_t meshIndex = node->mMeshes[meshRefIndex];
		if (scene->mNumMeshes <= meshIndex) {
			continue;
		}
		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh || mesh->mNumVertices == 0) {
			continue;
		}

		outPositions.reserve(outPositions.size() + mesh->mNumVertices);
		for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {

			outPositions.emplace_back(ToEnginePreviewPosition(mesh->mVertices[vertexIndex]));
		}
	}

	for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {

		CollectNodePositions(scene, node->mChildren[childIndex], outPositions);
	}
}

float Engine::ModelPreviewUtility::CalculateCameraDistance(const Vector3& min, const Vector3& max,
	const Vector3& center, float pitchDegrees, float yawDegrees, float fovYDegrees,
	float aspectRatio, float distanceScale) {

	const float halfFovY = (fovYDegrees * 0.5f) * 3.1415926535f / 180.0f;
	const float tanY = (std::max)(std::tan(halfFovY), 0.001f);
	const float tanX = (std::max)(tanY * (std::max)(aspectRatio, 0.001f), 0.001f);

	const Matrix4x4 rotation = Matrix4x4::MakeRotateMatrix(Vector3(pitchDegrees, yawDegrees, 0.0f));
	const Matrix4x4 inverseRotation = Matrix4x4::Inverse(rotation);

	float requiredDistance = 0.1f;
	for (int32_t ix = 0; ix < 2; ++ix) {
		for (int32_t iy = 0; iy < 2; ++iy) {
			for (int32_t iz = 0; iz < 2; ++iz) {

				const Vector3 corner(
					ix == 0 ? min.x : max.x,
					iy == 0 ? min.y : max.y,
					iz == 0 ? min.z : max.z);
				const Vector3 local = Vector3::TransferNormal(corner - center, inverseRotation);
				requiredDistance = (std::max)(requiredDistance, std::fabs(local.x) / tanX - local.z);
				requiredDistance = (std::max)(requiredDistance, std::fabs(local.y) / tanY - local.z);
			}
		}
	}
	return (std::max)(requiredDistance, 0.1f) * (std::max)(distanceScale, 1.0f) * 1.08f;
}

void Engine::ModelPreviewUtility::ImportReferencedTextures(AssetDatabase& database, AssetID meshAssetID) {

	if (!meshAssetID) {
		return;
	}

	const std::filesystem::path fullPath = database.ResolveFullPath(meshAssetID);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return;
	}

	TextureAssetResolver textureResolver{};
	textureResolver.Build(fullPath);

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fullPath.string(), kModelPreviewAssimpFlags);
	if (!scene || scene->mNumMaterials == 0) {
		return;
	}

	auto importTexture = [&](const std::string& reference) {

		const std::string assetPath = textureResolver.ResolveAssetPath(reference);
		if (!assetPath.empty()) {

			database.ImportOrGet(assetPath, AssetType::Texture);
		}
		};

	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {

		aiMaterial* material = scene->mMaterials[materialIndex];
		importTexture(AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }));
		importTexture(AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT }));
		importTexture(AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN }));
		importTexture(AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_SPECULAR }));
		importTexture(AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR }));
		importTexture(AssimpMaterialTextureExtractor::Extract(material, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP }));
	}
}

bool Engine::ModelPreviewUtility::ComputeBounds(const AssetDatabase& database, AssetID meshAssetID,
	Vector3& outMin, Vector3& outMax, Vector3& outCenter, float& outRadius) {

	if (!meshAssetID) {
		return false;
	}

	const std::filesystem::path fullPath = database.ResolveFullPath(meshAssetID);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return false;
	}

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fullPath.string(), kModelPreviewAssimpFlags);
	if (!scene || !scene->HasMeshes()) {
		return false;
	}

	std::vector<Vector3> positions{};
	CollectNodePositions(scene, scene->mRootNode, positions);
	if (positions.empty()) {
		return false;
	}

	Vector3 minV(FLT_MAX, FLT_MAX, FLT_MAX);
	Vector3 maxV(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const Vector3& p : positions) {

		minV.x = (std::min)(minV.x, p.x);
		minV.y = (std::min)(minV.y, p.y);
		minV.z = (std::min)(minV.z, p.z);
		maxV.x = (std::max)(maxV.x, p.x);
		maxV.y = (std::max)(maxV.y, p.y);
		maxV.z = (std::max)(maxV.z, p.z);
	}

	outMin = minV;
	outMax = maxV;
	outCenter = (minV + maxV) * 0.5f;

	float radius = 0.0f;
	for (const Vector3& p : positions) {

		radius = (std::max)(radius, (p - outCenter).Length());
	}
	outRadius = (std::max)(radius, 0.1f);
	return true;
}
