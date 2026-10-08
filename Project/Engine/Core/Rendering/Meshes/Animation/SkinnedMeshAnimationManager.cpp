#include "SkinnedMeshAnimationManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Rendering/Meshes/SkeletonBuilder.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileIOSystem.h>

// c++
#include <algorithm>
#include <exception>
#include <utility>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

//============================================================================
//	SkinnedMeshAnimationManager classMethods
//============================================================================
namespace {

	// アニメーションの名前を解決
	std::string ResolveClipName(const aiAnimation* anim, uint32_t index, uint32_t totalCount) {

		if (anim->mName.length > 0) {
			return anim->mName.C_Str();
		}
		if (totalCount == 1) {
			return "Default";
		}
		return "Clip_" + std::to_string(index);
	}
	// 行列を現在のエンジン座標系へ変換する共通関数
	Engine::Matrix4x4 ConvertAssimpAffineToEngine(const aiMatrix4x4& matrix) {

		aiVector3D scale{};
		aiVector3D translate{};
		aiQuaternion rotate{};
		matrix.Decompose(scale, rotate, translate);

		Engine::Vector3 engineScale(scale.x, scale.y, scale.z);
		Engine::Quaternion engineRotate(rotate.x, -rotate.y, -rotate.z, rotate.w);
		Engine::Vector3 engineTranslate(-translate.x, translate.y, translate.z);

		return Engine::Matrix4x4::MakeAffineMatrix(engineScale, engineRotate.Normalize(), engineTranslate);
	}
}

Engine::SkinnedMeshAnimationManager::~SkinnedMeshAnimationManager() {

	Finalize();
}

void Engine::SkinnedMeshAnimationManager::Init(uint32_t threadCount) {

	// ワーカープールを開始
	workerPool_.Start((std::max)(1u, threadCount),
		[this](LoadJob&& job, uint32_t workerIndex) { LoadJobAsync(std::move(job), workerIndex); });
}

void Engine::SkinnedMeshAnimationManager::Finalize() {

	workerPool_.Stop();

	std::scoped_lock lock(mutex_);
	loaded_.clear();
	requests_.clear();
}

void Engine::SkinnedMeshAnimationManager::RequestLoadAsync(AssetDatabase& assetDatabase, AssetID meshAssetID) {

	if (!meshAssetID) {
		return;
	}
	LoadJob job;
	{

		std::scoped_lock lock(mutex_);
		RequestState& request = requests_[meshAssetID];
		const uint64_t contentRevision = assetDatabase.GetContentRevision(meshAssetID);
		const uint64_t structureRevision = assetDatabase.GetStructureRevision();
		const bool first = request.serial == 0;
		bool changed = first || request.contentRevision != contentRevision;
		if (first || request.structureRevision != structureRevision) {

			const auto path = assetDatabase.ResolveFullPath(meshAssetID);
			changed |= path != request.fullPath;
			request.fullPath = path;
		}
		request.structureRevision = structureRevision;
		request.contentRevision = contentRevision;
		if (!changed) {
			return;
		}
		// 読込中に再更新された要求は別の番号で保持する
		request.serial = nextSerial_++;
		if (request.fullPath.empty()) {
			return;
		}
		job = {meshAssetID, request.fullPath, request.serial};
	}
	const uint64_t serial = job.serial;
	if (!workerPool_.Enqueue(std::move(job))) {

		std::scoped_lock lock(mutex_);
		const auto found = requests_.find(meshAssetID);
		if (found != requests_.end() && found->second.serial == serial) {
			requests_.erase(found);
		}
	}
}

void Engine::SkinnedMeshAnimationManager::WaitAll() {

	workerPool_.WaitIdle();
}

std::shared_ptr<const Engine::SkinnedMeshAnimationSet> Engine::SkinnedMeshAnimationManager::Find(AssetID meshAssetID) const {

	std::scoped_lock lock(mutex_);
	auto it = loaded_.find(meshAssetID);
	if (it == loaded_.end()) {
		return nullptr;
	}
	return it->second;
}

void Engine::SkinnedMeshAnimationManager::LoadJobAsync(LoadJob&& job, [[maybe_unused]] uint32_t workerIndex) {

	// ファイルのインポート
	std::shared_ptr<SkinnedMeshAnimationSet> imported;
	bool succeeded = false;
	try {

		imported = std::make_shared<SkinnedMeshAnimationSet>(ImportAnimationFile(job.meshAssetID, job.fullPath));
		succeeded = imported->valid;
	} catch (const std::exception& exception) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"SkinnedMesh Animationの非同期読み込み中に例外が発生しました path={} 内容={}", Algorithm::PathToUTF8(job.fullPath),
			exception.what());
		succeeded = false;
	} catch (...) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"SkinnedMesh Animationの非同期読み込み中に不明な例外が発生しました path={}", Algorithm::PathToUTF8(job.fullPath));
		succeeded = false;
	}
	// 結果の保存
	{
		std::scoped_lock lock(mutex_);
		const auto request = requests_.find(job.meshAssetID);
		if (succeeded && request != requests_.end() && request->second.serial == job.serial) {

			// 完成した世代を公開し、共有中の旧世代は保持する
			imported->generation = nextGeneration_++;
			loaded_[job.meshAssetID] = std::move(imported);
		}
	}
}

Engine::SkinnedMeshAnimationSet Engine::SkinnedMeshAnimationManager::ImportAnimationFile(
	AssetID meshAssetID, const std::filesystem::path& fullPath) const {

	SkinnedMeshAnimationSet result{};
	result.meshAssetID = meshAssetID;

	Assimp::Importer importer;
	auto* fileSystem = new ModelFileIOSystem(fullPath);
	importer.SetIOHandler(fileSystem);
	const aiScene* scene = importer.ReadFile(fileSystem->GetModelPath(), aiProcess_PopulateArmatureData);
	if (!scene || !scene->mRootNode) {
		return result;
	}

	// スキンボーンと必要な親ノードからスケルトンを構築
	result.skeleton = BuildSkinSkeleton(scene, Algorithm::PathToUTF8(fullPath));
	if (result.skeleton.joints.empty()) {
		return result;
	}

	result.skinCluster.inverseBindPoseMatrices.resize(result.skeleton.joints.size(), Matrix4x4::Identity());
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh || !mesh->HasBones()) {
			continue;
		}
		for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {

			const aiBone* bone = mesh->mBones[boneIndex];
			if (!bone) {
				continue;
			}
			const int32_t jointIndex = FindSkeletonJointIndex(result.skeleton, bone);
			if (jointIndex < 0) {
				continue;
			}

			const aiMatrix4x4& m = bone->mOffsetMatrix;

			// ボーンのオフセット行列を現在のエンジン座標系に変換して保存
			result.skinCluster.inverseBindPoseMatrices[jointIndex] = ConvertAssimpAffineToEngine(m);
		}
	}
	// アニメーションの数だけループしてアニメーションクリップを構築
	for (uint32_t animIndex = 0; animIndex < scene->mNumAnimations; ++animIndex) {

		const aiAnimation* anim = scene->mAnimations[animIndex];
		if (!anim) {
			continue;
		}

		// アニメーションデータを構築
		AnimationData clip{};

		// 再生時間の計算に必要な情報があるか
		bool validDuration = 0.0 < anim->mTicksPerSecond;

		// 再生時間
		clip.duration =
			validDuration ? static_cast<float>(anim->mDuration / anim->mTicksPerSecond) : static_cast<float>(anim->mDuration);

		for (uint32_t c = 0; c < anim->mNumChannels; ++c) {

			const aiNodeAnim* nodeAnim = anim->mChannels[c];
			if (!nodeAnim) {
				continue;
			}

			const int32_t jointIndex = FindSkeletonJointIndex(result.skeleton, nodeAnim->mNodeName.C_Str());
			if (jointIndex < 0) {
				continue;
			}
			NodeAnimation& dst = clip.nodeAnimations[result.skeleton.joints[jointIndex].nodePath];
			// キーフレーム構築
			// 座標
			for (uint32_t k = 0; k < nodeAnim->mNumPositionKeys; ++k) {

				const aiVectorKey& kv = nodeAnim->mPositionKeys[k];
				KeyframeVector3 f{};
				f.time = validDuration ? static_cast<float>(kv.mTime / anim->mTicksPerSecond) : static_cast<float>(kv.mTime);
				f.value = {-kv.mValue.x, kv.mValue.y, kv.mValue.z};
				dst.translate.keyframes.emplace_back(f);
			}
			// 回転
			for (uint32_t k = 0; k < nodeAnim->mNumRotationKeys; ++k) {

				const aiQuatKey& kv = nodeAnim->mRotationKeys[k];
				KeyframeQuaternion f{};
				f.time = validDuration ? static_cast<float>(kv.mTime / anim->mTicksPerSecond) : static_cast<float>(kv.mTime);
				f.value = {kv.mValue.x, -kv.mValue.y, -kv.mValue.z, kv.mValue.w};
				dst.rotate.keyframes.emplace_back(f);
			}
			// スケール
			for (uint32_t k = 0; k < nodeAnim->mNumScalingKeys; ++k) {

				const aiVectorKey& kv = nodeAnim->mScalingKeys[k];
				KeyframeVector3 f{};
				f.time = validDuration ? static_cast<float>(kv.mTime / anim->mTicksPerSecond) : static_cast<float>(kv.mTime);
				f.value = {kv.mValue.x, kv.mValue.y, kv.mValue.z};
				dst.scale.keyframes.emplace_back(f);
			}
		}
		// アニメーションの名前を取得
		std::string clipName = ResolveClipName(anim, animIndex, scene->mNumAnimations);
		// クリップの名前が重複していないか確認してから保存
		if (!result.clips.contains(clipName)) {

			result.clipOrder.emplace_back(clipName);
		}
		result.clips[clipName] = std::move(clip);
	}

	// Clip別にJointへ対応するTrack参照を作る
	result.clipJointTracks.reserve(result.clips.size());
	for (auto& [clipName, clip] : result.clips) {

		std::vector<const NodeAnimation*>& tracks = result.clipJointTracks[clipName];
		tracks.assign(result.skeleton.joints.size(), nullptr);
		for (const Joint& joint : result.skeleton.joints) {

			auto it = clip.nodeAnimations.find(joint.nodePath);
			if (it != clip.nodeAnimations.end()) {
				tracks[joint.index] = &it->second;
			}
		}
	}

	// 骨格とClipの両方がある定義を有効にする
	result.valid = !result.skeleton.joints.empty() && !result.clips.empty();
	return result;
}
