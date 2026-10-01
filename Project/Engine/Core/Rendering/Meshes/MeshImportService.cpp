#include "MeshImportService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshletBuilder.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

#include <Engine/Core/Rendering/Meshes/Import/AssimpMaterialTextureExtractor.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportUtility.h>

// c++
#include <cmath>
#include <span>

//============================================================================
//	MeshImportService classMethods
//============================================================================
namespace {

	// 頂点のジョイント影響を正規化
	void NormalizeInfluence(Engine::VertexInfluence& influence) {

		float sum = 0.0f;
		for (float w : influence.weights) {
			if (!std::isfinite(w) || w < 0.0f) {
				influence = Engine::VertexInfluence{};
				return;
			}
			sum += w;
		}

		if (!std::isfinite(sum) || sum <= 0.0f) {
			influence = Engine::VertexInfluence{};
			return;
		}

		for (float& w : influence.weights) {
			w /= sum;
		}
	}
	void InsertInfluenceTop(Engine::VertexInfluence& dst, int32_t jointIndex, float weight) {

		if (jointIndex < 0 || !std::isfinite(weight) || weight <= 0.0f) {
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

Engine::MeshImportService::~MeshImportService() {

	// 結果の保存先を破棄する前にワーカーを終了する
	Finalize();
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
	failed_.clear();
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
	const AssetMeta* meta = assetDatabase.Find(meshAssetID);
	const MeshImportSettings settings = meta ?
		ParseMeshImportSettings(meta->importerSettings) :
		MeshImportSettings{};
	std::array<std::filesystem::path, 3> manualLODPaths{};
	for (size_t index = 0; index < settings.manualLODMeshes.size(); ++index) {
		const AssetID manualAsset = settings.manualLODMeshes[index];
		const AssetMeta* manualMeta = manualAsset ?
			assetDatabase.Find(manualAsset) : nullptr;
		if (manualMeta && manualMeta->type == AssetType::Mesh &&
			manualAsset != meshAssetID) {

			manualLODPaths[index] =
				assetDatabase.ResolveFullPath(manualAsset);
		}
	}

	{
		std::scoped_lock lock(mutex_);
		if (imported_.contains(meshAssetID) || queued_.contains(meshAssetID) || loading_.contains(meshAssetID)) {
			return false;
		}
		queued_.insert(meshAssetID);
		failed_.erase(meshAssetID);
	}

	// ジョブをワーカープールに追加
	if (!workerPool_.Enqueue(MeshLoadJob{
		.assetID = meshAssetID,
		.fullPath = std::move(fullPath),
		.settings = settings,
		.manualLODPaths = std::move(manualLODPaths),
		})) {
		// 受付を断った要求を待機中として残さない
		std::scoped_lock lock(mutex_);
		queued_.erase(meshAssetID);
		return false;
	}

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

bool Engine::MeshImportService::ConsumeFailed(AssetID meshAssetID) {

	std::scoped_lock lock(mutex_);
	return failed_.erase(meshAssetID) != 0;
}

bool Engine::MeshImportService::IsPending(AssetID meshAssetID) const {

	std::scoped_lock lock(mutex_);
	return queued_.contains(meshAssetID) || loading_.contains(meshAssetID);
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
		imported = ImportFile(
			job.assetID, job.fullPath, job.settings,
			job.manualLODPaths);
		succeeded = !imported.vertices.empty() && !imported.indices.empty();
	}
	catch (const std::exception& exception) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"Meshの非同期読み込み中に例外が発生しました path={} 内容={}",
			Algorithm::PathToUTF8(job.fullPath), exception.what());
		succeeded = false;
	}
	catch (...) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"Meshの非同期読み込み中に不明な例外が発生しました path={}",
			Algorithm::PathToUTF8(job.fullPath));
		succeeded = false;
	}
	// 結果の保存
	{
		std::scoped_lock lock(mutex_);
		loading_.erase(job.assetID);
		if (succeeded) {
			imported_[job.assetID] = std::move(imported);
		} else {
			failed_.insert(job.assetID);
		}
	}
}

Engine::ImportedMeshAsset Engine::MeshImportService::ImportFile(
	AssetID assetID, const std::filesystem::path& fullPath,
	const MeshImportSettings& settings,
	const std::array<std::filesystem::path, 3>& manualLODPaths,
	bool buildGPUData) const {

	// 基本情報を設定
	ImportedMeshAsset result{};
	result.assetID = assetID;
	result.sourcePath = Algorithm::ConvertString(fullPath.generic_wstring());
	result.ditherLODTransitions =
		settings.lodTransition == MeshLODTransitionMode::Dither;

	// テクスチャアセット参照を解決
	TextureAssetResolver textureResolver{};
	textureResolver.Build(fullPath);

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(Algorithm::PathToUTF8(fullPath),
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_PopulateArmatureData |
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
		if (!MeshImportUtility::HasTriangleGeometry(mesh)) {
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

		skeleton = BuildSkinSkeleton(scene, Algorithm::PathToUTF8(fullPath));
		if (!skeleton.joints.empty()) {

			result.isSkinned = true;
			result.boneCount = static_cast<uint32_t>(skeleton.joints.size());
			result.skeletonJointPaths.reserve(skeleton.joints.size());
			for (const Joint& joint : skeleton.joints) {
				result.skeletonJointPaths.emplace_back(joint.nodePath);
			}
			result.vertexInfluences.resize(totalVertexCount);
		}
	}

	uint32_t globalVertexOffset = 0;
	std::vector<uint32_t> meshBaseVertexOffsets(scene->mNumMeshes, 0);
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

		aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!MeshImportUtility::HasTriangleGeometry(mesh)) {
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
			aiVector3D tangent = mesh->HasTangentsAndBitangents() ? mesh->mTangents[v] : aiVector3D(1.0f, 0.0f, 0.0f);

			// 頂点データを変換して保存
			MeshVertex vertex{};
			vertex.position = Vector4(-pos.x, pos.y, pos.z, 1.0f);
			vertex.normal = Vector3(-normal.x, normal.y, normal.z);
			vertex.tangent = Vector3(-tangent.x, tangent.y, tangent.z);
			vertex.uv = Vector2(uv.x, uv.y);

			// 接線の利き手を、左右手系変換後のnormal/tangent/bitangentから求める
			// X反転(左右手変換)でbitangentの符号も反転するため、ここで一括して符号を確定させる
			// bitangentが無いモデルは+1にフォールバックする
			if (mesh->HasTangentsAndBitangents()) {

				const aiVector3D bitangent = mesh->mBitangents[v];
				const Vector3 convertedBitangent = Vector3(-bitangent.x, bitangent.y, bitangent.z);
				const Vector3 expectedBitangent = Vector3::Cross(vertex.normal, vertex.tangent);
				vertex.tangentSign = (Vector3::Dot(expectedBitangent, convertedBitangent) < 0.0f) ? -1.0f : 1.0f;
			} else {

				vertex.tangentSign = 1.0f;
			}
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
			subMesh.name = Engine::MeshImportUtility::BuildSubMeshName(mesh, meshIndex, material);

			// マテリアルがあれば、テクスチャの参照を取得
			if (material) {
				const MeshImportUtility::ImportedMaterialSurface surface =
					MeshImportUtility::ReadMaterialSurface(material);
				subMesh.surfaceMode = surface.surfaceMode;
				subMesh.alphaCutoff = surface.alphaCutoff;

				aiString materialName;
				if (material->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS && materialName.length > 0) {
					subMesh.materialName = materialName.C_Str();
				}

				// 宣言の有無と、実際に解決できた画像を区別する
				subMesh.hasBaseColorTexture = !AssimpMaterialTextureExtractor::Extract(
					material, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE }).empty();
				subMesh.defaultTextures = AssimpMaterialTextureExtractor::ExtractResolved(material, textureResolver);

				// 編集用layoutと同じ優先順位で色と透明度を取得する
				subMesh.baseColor = MeshImportUtility::ReadMaterialFactors(material).baseColor;
			}
			result.subMeshes.emplace_back(std::move(subMesh));
		}

		globalVertexOffset += mesh->mNumVertices;
	}

	// 骨が入っていれば、ジョイントの影響を頂点に割り当てる
	if (result.isSkinned) {

		for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

			aiMesh* mesh = scene->mMeshes[meshIndex];
			if (!MeshImportUtility::HasTriangleGeometry(mesh) || !mesh->HasBones()) {
				continue;
			}

			uint32_t baseVertexOffset = meshBaseVertexOffsets[meshIndex];
			for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {

				const aiBone* bone = mesh->mBones[boneIndex];
				if (!bone) {
					continue;
				}

				const int32_t jointIndex = FindSkeletonJointIndex(skeleton, bone);
				if (jointIndex < 0) {
					continue;
				}

				for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {

					const aiVertexWeight& weight = bone->mWeights[weightIndex];
					// 元Meshの範囲外の重みを次のMeshへ流さない
					if (weight.mVertexId >= mesh->mNumVertices) {
						continue;
					}
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

	if (!buildGPUData) {
		return result;
	}

	// 手動LODをLOD0と同じバッファへ連結
	result.lods[0].indexOffset = 0;
	result.lods[0].indexCount =
		static_cast<uint32_t>(result.indices.size());
	for (SubMeshDesc& subMesh : result.subMeshes) {
		subMesh.lods[0].indexOffset = subMesh.indexOffset;
		subMesh.lods[0].indexCount = subMesh.indexCount;
	}
	for (size_t sourceIndex = 0;
		sourceIndex < manualLODPaths.size(); ++sourceIndex) {

		const std::filesystem::path& lodPath =
			manualLODPaths[sourceIndex];
		if (lodPath.empty()) {
			continue;
		}
		const uint32_t lodIndex =
			static_cast<uint32_t>(sourceIndex + 1);
		const ImportedMeshAsset source = ImportFile(
			{}, lodPath, MeshImportSettings{}, {}, false);
		if (source.vertices.empty() || source.indices.empty() ||
			source.subMeshes.size() != result.subMeshes.size() ||
			source.isSkinned != result.isSkinned ||
			source.boneCount != result.boneCount ||
			source.skeletonJointPaths != result.skeletonJointPaths) {

			Logger::Output(LogType::Engine, spdlog::level::warn,
				"Meshの手動LODを適用できません path={}",
				Algorithm::PathToUTF8(lodPath));
			continue;
		}

		const uint32_t vertexOffset =
			static_cast<uint32_t>(result.vertices.size());
		std::vector<uint32_t> subMeshMap(
			source.subMeshes.size(), UINT32_MAX);
		std::vector<bool> targetUsed(result.subMeshes.size(), false);
		for (uint32_t index = 0;
			index < static_cast<uint32_t>(source.subMeshes.size()); ++index) {

			const std::string& name = source.subMeshes[index].name;
			auto found = std::find_if(
				result.subMeshes.begin(), result.subMeshes.end(),
				[&](const SubMeshDesc& target) {
					return !name.empty() && target.name == name;
				});
			uint32_t targetIndex = found != result.subMeshes.end() ?
				static_cast<uint32_t>(found - result.subMeshes.begin()) : index;
			if (targetIndex >= targetUsed.size() || targetUsed[targetIndex]) {
				subMeshMap.clear();
				break;
			}
			subMeshMap[index] = targetIndex;
			targetUsed[targetIndex] = true;
		}
		if (subMeshMap.size() != source.subMeshes.size()) {
			Logger::Output(LogType::Engine, spdlog::level::warn,
				"Meshの手動LODのSubMesh対応を解決できません path={}",
				Algorithm::PathToUTF8(lodPath));
			continue;
		}

		result.vertices.insert(result.vertices.end(),
			source.vertices.begin(), source.vertices.end());
		for (uint32_t sourceSubMesh : source.vertexSubMeshIndices) {
			result.vertexSubMeshIndices.emplace_back(
				sourceSubMesh < subMeshMap.size() ?
					subMeshMap[sourceSubMesh] : 0u);
		}
		if (result.isSkinned) {
			result.vertexInfluences.insert(
				result.vertexInfluences.end(),
				source.vertexInfluences.begin(),
				source.vertexInfluences.end());
		}

		MeshLODRange& lod = result.lods[lodIndex];
		lod.indexOffset = static_cast<uint32_t>(result.indices.size());
		for (uint32_t index = 0;
			index < static_cast<uint32_t>(source.subMeshes.size()); ++index) {

			const SubMeshDesc& sourceSubMesh = source.subMeshes[index];
			MeshLODRange& targetRange =
				result.subMeshes[subMeshMap[index]].lods[lodIndex];
			targetRange.indexOffset =
				static_cast<uint32_t>(result.indices.size());
			const uint32_t* begin =
				source.indices.data() + sourceSubMesh.indexOffset;
			for (uint32_t sourceVertex : std::span(
				begin, sourceSubMesh.indexCount)) {

				result.indices.emplace_back(
					sourceVertex + vertexOffset);
			}
			targetRange.indexCount = sourceSubMesh.indexCount;
		}
		lod.indexCount =
			static_cast<uint32_t>(result.indices.size()) -
			lod.indexOffset;
		result.authoredLODs[lodIndex] = true;
	}

	// メッシュレットの構築
	MeshletBuilder builder{};
	builder.Build(result, settings);

	return result;
}

Engine::MeshNode Engine::MeshImportService::ReadNode(aiNode* node) const {

	return MeshImportUtility::ReadMeshNodeTree(node);
}
