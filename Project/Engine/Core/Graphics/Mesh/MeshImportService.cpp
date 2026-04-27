#include "MeshImportService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Texture/TextureAssetResolver.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshletBuilder.h>
#include <Engine/MathLib/Matrix4x4.h>

//============================================================================
//	MeshImportService classMethods
//============================================================================

namespace {

	// サブメッシュの名前を構築
	std::string BuildSubMeshName(const aiMesh* mesh, uint32_t meshIndex, const aiMaterial* material) {

		if (mesh && mesh->mName.length > 0) {
			return mesh->mName.C_Str();
		}
		if (material) {
			aiString materialName;
			if (material->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS && materialName.length > 0) {
				return materialName.C_Str();
			}
		}
		return "SubMesh_" + std::to_string(meshIndex);
	}
	// マテリアルからテクスチャの参照を取得
	std::string GetMaterialTextureReference(aiMaterial* material, std::initializer_list<aiTextureType> textureTypes) {

		if (!material) {
			return {};
		}
		for (aiTextureType type : textureTypes) {

			if (material->GetTextureCount(type) == 0) {
				continue;
			}
			aiString textureName;
			if (material->GetTexture(type, 0, &textureName) == AI_SUCCESS && 0 < textureName.length) {
				return textureName.C_Str();
			}
		}
		return {};
	}
	// メッシュノードからスケルトンを構築するための再帰関数
	int32_t CreateJointRecursive(const Engine::MeshNode& node,
		const std::optional<int32_t> parent, std::vector<Engine::Joint>& joints) {

		Engine::Joint joint{};
		joint.name = node.name;
		joint.localMatrix = node.localMatrix;
		joint.transform = node.transform;
		joint.index = static_cast<int32_t>(joints.size());
		joint.parent = parent;

		joints.emplace_back(joint);
		for (const auto& child : node.children) {

			int32_t childIndex = CreateJointRecursive(child, joint.index, joints);
			joints[joint.index].children.emplace_back(childIndex);
		}
		return joint.index;
	}
	// メッシュノードからスケルトンを構築
	Engine::Skeleton BuildSkeletonFromMeshNode(const Engine::MeshNode& rootNode) {

		Engine::Skeleton skeleton{};
		skeleton.root = CreateJointRecursive(rootNode, std::nullopt, skeleton.joints);
		for (const auto& joint : skeleton.joints) {
			skeleton.jointMap.emplace(joint.name, joint.index);
		}
		return skeleton;
	}
	// 頂点のジョイント影響を正規化
	void NormalizeInfluence(Engine::VertexInfluence& influence) {

		float sum = 0.0f;
		for (float w : influence.weights) {
			sum += w;
		}

		if (sum <= 0.0f) {
			return;
		}

		for (float& w : influence.weights) {
			w /= sum;
		}
	}
	void InsertInfluenceTop(Engine::VertexInfluence& dst, int32_t jointIndex, float weight) {

		if (jointIndex < 0 || weight <= 0.0f) {
			return;
		}

		uint32_t minSlot = 0;
		for (uint32_t i = 1; i < Engine::kNumMaxInfluence; ++i) {
			if (dst.weights[i] < dst.weights[minSlot]) {

				minSlot = i;
			}
		}
		if (weight <= dst.weights[minSlot]) {
			return;
		}
		dst.weights[minSlot] = weight;
		dst.jointIndices[minSlot] = jointIndex;
	}
}

void Engine::MeshImportService::Init(uint32_t threadCount) {

	// ワーカープールの開始
	workerPool_.Start((std::max)(1u, threadCount),
		[this](MeshLoadJob&& job, uint32_t workerIndex) {
			LoadJob(std::move(job), workerIndex);
		});
}

void Engine::MeshImportService::Finalize() {

	workerPool_.Stop();

	std::scoped_lock lock(mutex_);
	imported_.clear();
	queued_.clear();
	loading_.clear();
}

bool Engine::MeshImportService::RequestLoadAsync(AssetDatabase& assetDatabase, AssetID meshAssetID) {

	// 無効なIDは無視
	if (!meshAssetID) {
		return false;
	}
	{
		std::scoped_lock lock(mutex_);
		if (imported_.contains(meshAssetID) || queued_.contains(meshAssetID) || loading_.contains(meshAssetID)) {
			return false;
		}
	}

	// アセットデータベースからフルパスを解決して存在を確認
	std::filesystem::path fullPath = assetDatabase.ResolveFullPath(meshAssetID);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return false;
	}

	{
		std::scoped_lock lock(mutex_);
		if (imported_.contains(meshAssetID) || queued_.contains(meshAssetID) || loading_.contains(meshAssetID)) {
			return false;
		}
		queued_.insert(meshAssetID);
	}

	// ジョブをワーカープールに追加
	workerPool_.Enqueue(MeshLoadJob{ .assetID = meshAssetID,.fullPath = std::move(fullPath), });

	return true;
}

bool Engine::MeshImportService::TakeImported(AssetID meshAssetID, ImportedMeshAsset& outImported) {

	std::scoped_lock lock(mutex_);
	auto it = imported_.find(meshAssetID);
	if (it == imported_.end()) {
		return false;
	}
	outImported = std::move(it->second);
	imported_.erase(it);
	return true;
}

bool Engine::MeshImportService::IsLoaded(AssetID meshAssetID) const {

	std::scoped_lock lock(mutex_);
	return imported_.contains(meshAssetID);
}

void Engine::MeshImportService::WaitAll() {

	workerPool_.WaitIdle();
}

void Engine::MeshImportService::LoadJob(MeshLoadJob&& job, [[maybe_unused]] uint32_t workerIndex) {

	{
		std::scoped_lock lock(mutex_);
		queued_.erase(job.assetID);
		loading_.insert(job.assetID);
	}

	// ファイルのインポート
	ImportedMeshAsset imported{};
	bool succeeded = false;
	try {
		imported = ImportFile(job.assetID, job.fullPath);
		succeeded = !imported.vertices.empty() && !imported.indices.empty();
	}
	catch (...) {
		succeeded = false;
	}
	// 結果の保存
	{
		std::scoped_lock lock(mutex_);
		loading_.erase(job.assetID);
		if (succeeded) {
			imported_[job.assetID] = std::move(imported);
		}
	}
}

Engine::ImportedMeshAsset Engine::MeshImportService::ImportFile(AssetID assetID,
	const std::filesystem::path& fullPath) const {

	// 基本情報を設定
	ImportedMeshAsset result{};
	result.assetID = assetID;
	result.sourcePath = fullPath.generic_string();

	// テクスチャアセット参照を解決
	TextureAssetResolver textureResolver{};
	textureResolver.Build(fullPath);

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fullPath.string(),
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_SortByPType);

	if (!scene || !scene->HasMeshes()) {
		return result;
	}

	// ノード階層構築
	result.rootNode = ReadNode(scene->mRootNode);

	// メッシュデータの集計
	bool containsSkinnedMesh = false;
	size_t totalVertexCount = 0;
	size_t totalIndexCount = 0;
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

		aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
			continue;
		}

		totalVertexCount += mesh->mNumVertices;
		totalIndexCount += static_cast<size_t>(mesh->mNumFaces) * 3;
		// ボーンを持つメッシュがあるかどうかを確認
		if (mesh->HasBones() && mesh->mNumBones > 0) {
			containsSkinnedMesh = true;
		}
	}

	result.vertices.reserve(totalVertexCount);
	result.indices.reserve(totalIndexCount);
	result.subMeshes.reserve(scene->mNumMeshes);
	result.vertexSubMeshIndices.reserve(totalVertexCount);

	// スキニング情報の構築
	Skeleton skeleton{};
	if (containsSkinnedMesh) {

		skeleton = BuildSkeletonFromMeshNode(result.rootNode);
		result.isSkinned = true;
		result.boneCount = static_cast<uint32_t>(skeleton.joints.size());
		result.vertexInfluences.resize(totalVertexCount);
	}

	uint32_t globalVertexOffset = 0;
	std::vector<uint32_t> meshBaseVertexOffsets(scene->mNumMeshes, 0);
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

		aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
			continue;
		}

		// メッシュの頂点オフセットを記録
		meshBaseVertexOffsets[meshIndex] = globalVertexOffset;

		// メッシュに関連付けられたマテリアルを取得
		aiMaterial* material = (mesh->mMaterialIndex < scene->mNumMaterials) ?
			scene->mMaterials[mesh->mMaterialIndex] : nullptr;

		// サブメッシュのインデックスを記録
		uint32_t subMeshIndex = static_cast<uint32_t>(result.subMeshes.size());
		// サブメッシュのインデックスオフセットを記録
		uint32_t subMeshIndexOffset = static_cast<uint32_t>(result.indices.size());

		for (uint32_t v = 0; v < mesh->mNumVertices; ++v) {

			aiVector3D pos = mesh->mVertices[v];
			aiVector3D normal = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0.0f, 1.0f, 0.0f);
			aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][v] : aiVector3D(0.0f, 0.0f, 0.0f);

			// 頂点データを変換して保存
			MeshVertex vertex{};
			vertex.position = Vector4(-pos.x, pos.y, pos.z, 1.0f);
			vertex.normal = Vector3(-normal.x, normal.y, normal.z);
			vertex.uv = Vector2(uv.x, uv.y);
			result.vertices.emplace_back(vertex);

			// 頂点が属するサブメッシュのインデックスを保存
			result.vertexSubMeshIndices.emplace_back(subMeshIndex);
		}

		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {

			const aiFace& face = mesh->mFaces[faceIndex];
			if (face.mNumIndices != 3) {
				continue;
			}

			result.indices.emplace_back(globalVertexOffset + face.mIndices[0]);
			result.indices.emplace_back(globalVertexOffset + face.mIndices[1]);
			result.indices.emplace_back(globalVertexOffset + face.mIndices[2]);
		}

		uint32_t subMeshIndexCount = static_cast<uint32_t>(result.indices.size()) - subMeshIndexOffset;
		if (0 < subMeshIndexCount) {

			// サブメッシュデータ構築
			SubMeshDesc subMesh{};
			subMesh.indexOffset = subMeshIndexOffset;
			subMesh.indexCount = subMeshIndexCount;
			subMesh.name = BuildSubMeshName(mesh, meshIndex, material);

			// マテリアルがあれば、テクスチャの参照を取得
			if (material) {

				aiString materialName;
				if (material->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS && materialName.length > 0) {
					subMesh.materialName = materialName.C_Str();
				}

				// デフォルトで設定されているテクスチャのパスを取得
				subMesh.defaultTextures.baseColorTexturePath = textureResolver.ResolveAssetPath(
					GetMaterialTextureReference(material, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }));
				subMesh.defaultTextures.normalTexturePath = textureResolver.ResolveAssetPath(
					GetMaterialTextureReference(material, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT }));
				subMesh.defaultTextures.metallicRoughnessTexturePath = textureResolver.ResolveAssetPath(
					GetMaterialTextureReference(material, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN }));
				subMesh.defaultTextures.specularTexturePath = textureResolver.ResolveAssetPath(
					GetMaterialTextureReference(material, { aiTextureType_SPECULAR }));
				subMesh.defaultTextures.emissiveTexturePath = textureResolver.ResolveAssetPath(
					GetMaterialTextureReference(material, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR }));
				subMesh.defaultTextures.occlusionTexturePath = textureResolver.ResolveAssetPath(
					GetMaterialTextureReference(material, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP }));

				aiColor4D baseColor;
				if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor)) {
					subMesh.baseColor = Color4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
				}
			}
			result.subMeshes.emplace_back(std::move(subMesh));
		}

		globalVertexOffset += mesh->mNumVertices;
	}

	// 骨が入っていれば、ジョイントの影響を頂点に割り当てる
	if (result.isSkinned) {

		for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

			aiMesh* mesh = scene->mMeshes[meshIndex];
			if (!mesh || !mesh->HasBones()) {
				continue;
			}

			uint32_t baseVertexOffset = meshBaseVertexOffsets[meshIndex];
			for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {

				const aiBone* bone = mesh->mBones[boneIndex];
				if (!bone) {
					continue;
				}

				auto jointIt = skeleton.jointMap.find(bone->mName.C_Str());
				if (jointIt == skeleton.jointMap.end()) {
					continue;
				}

				int32_t jointIndex = jointIt->second;
				for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {

					const aiVertexWeight& weight = bone->mWeights[weightIndex];
					uint32_t globalIndex = baseVertexOffset + weight.mVertexId;
					if (globalIndex >= result.vertexInfluences.size()) {
						continue;
					}

					// 頂点にジョイントの影響を挿入
					InsertInfluenceTop(result.vertexInfluences[globalIndex], jointIndex, weight.mWeight);
				}
			}
		}

		// 影響を正規化
		for (auto& influence : result.vertexInfluences) {
			NormalizeInfluence(influence);
		}
	}

	// メッシュレットの構築
	MeshletBuilder builder{};
	builder.Build(result);

	return result;
}

Engine::MeshNode Engine::MeshImportService::ReadNode(aiNode* node) const {

	MeshNode result{};
	if (!node) {
		return result;
	}

	aiVector3D scale, translate;
	aiQuaternion rotate;
	node->mTransformation.Decompose(scale, rotate, translate);

	// ノードの変換をエンジンの座標系に合わせて変換して保存
	result.transform.scale = { scale.x, scale.y, scale.z };
	result.transform.rotation = { rotate.x, -rotate.y, -rotate.z, rotate.w };
	result.transform.translation = { -translate.x, translate.y, translate.z };
	result.localMatrix = Matrix4x4::MakeAffineMatrix(result.transform.scale, result.transform.rotation, result.transform.translation);
	result.name = node->mName.C_Str();
	result.children.resize(node->mNumChildren);
	for (uint32_t i = 0; i < node->mNumChildren; ++i) {

		result.children[i] = ReadNode(node->mChildren[i]);
	}
	return result;
}