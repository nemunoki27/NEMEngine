#include "SkinnedMeshAnimationManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/MathLib/Matrix4x4.h>

//============================================================================
//	SkinnedMeshAnimationManager classMethods
//============================================================================

namespace {

	// アニメーションのためのノードを再帰的に読み込む
	Engine::MeshNode ReadNodeForAnimation(aiNode* node) {

		Engine::MeshNode result{};
		if (!node) {
			return result;
		}

		aiVector3D scale, translate;
		aiQuaternion rotate;
		// Assimpのノードの変換行列をスケール、回転、平行移動に分解
		node->mTransformation.Decompose(scale, rotate, translate);

		result.transform.scale = { scale.x, scale.y, scale.z };
		result.transform.rotation = { rotate.x, -rotate.y, -rotate.z, rotate.w };
		result.transform.translation = { -translate.x, translate.y, translate.z };

		// スケール、回転、平行移動からローカル変換行列を作成
		result.localMatrix = Engine::Matrix4x4::MakeAffineMatrix(result.transform.scale,
			result.transform.rotation, result.transform.translation);
		result.name = node->mName.C_Str();

		// 子ノードも再帰的に読み込む
		result.children.resize(node->mNumChildren);
		for (uint32_t i = 0; i < node->mNumChildren; ++i) {

			result.children[i] = ReadNodeForAnimation(node->mChildren[i]);
		}
		return result;
	}

	int32_t CreateJointRecursive(const Engine::MeshNode& node,
		const std::optional<int32_t> parent, std::vector<Engine::Joint>& joints) {

		// Jointを作成して追加
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
	Engine::Skeleton BuildSkeleton(const Engine::MeshNode& rootNode) {

		Engine::Skeleton skeleton{};
		skeleton.root = CreateJointRecursive(rootNode, std::nullopt, skeleton.joints);
		for (const auto& joint : skeleton.joints) {

			skeleton.jointMap.emplace(joint.name, joint.index);
		}
		return skeleton;
	}
	// アニメーションの名前を解決
	std::string ResolveClipName(const aiAnimation* anim, uint32_t index, uint32_t totalCount) {

		if (anim && anim->mName.length > 0) {
			return anim->mName.C_Str();
		}
		if (totalCount == 1) {
			return "Default";
		}
		return "Clip_" + std::to_string(index);
	}
	//  行列を現在のエンジン座標系へ変換する共通関数
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
		[this](LoadJob&& job, uint32_t workerIndex) {
			LoadJobAsync(std::move(job), workerIndex);
		});
}

void Engine::SkinnedMeshAnimationManager::Finalize() {

	workerPool_.Stop();

	std::scoped_lock lock(mutex_);
	loaded_.clear();
	queued_.clear();
	loading_.clear();
}

void Engine::SkinnedMeshAnimationManager::RequestLoadAsync(
	AssetDatabase& assetDatabase, AssetID meshAssetID) {

	// 無効なIDは無視
	if (!meshAssetID) {
		return;
	}
	{
		std::scoped_lock lock(mutex_);
		if (loaded_.contains(meshAssetID) || queued_.contains(meshAssetID) || loading_.contains(meshAssetID)) {
			return;
		}
	}

	// アセットデータベースからフルパスを解決して存在を確認
	std::filesystem::path fullPath = assetDatabase.ResolveFullPath(meshAssetID);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return;
	}
	{
		std::scoped_lock lock(mutex_);
		queued_.insert(meshAssetID);
	}

	// ジョブをワーカープールに追加
	workerPool_.Enqueue(LoadJob{ .meshAssetID = meshAssetID,.fullPath = fullPath, });
}

void Engine::SkinnedMeshAnimationManager::WaitAll() {

	workerPool_.WaitIdle();
}

const Engine::SkinnedMeshAnimationSet* Engine::SkinnedMeshAnimationManager::Find(AssetID meshAssetID) const {

	std::scoped_lock lock(mutex_);
	auto it = loaded_.find(meshAssetID);
	if (it == loaded_.end()) {
		return nullptr;
	}
	return &it->second;
}

void Engine::SkinnedMeshAnimationManager::LoadJobAsync(LoadJob&& job, [[maybe_unused]] uint32_t workerIndex) {

	// ジョブの状態を更新
	{
		std::scoped_lock lock(mutex_);
		queued_.erase(job.meshAssetID);
		loading_.insert(job.meshAssetID);
	}

	// ファイルのインポート
	SkinnedMeshAnimationSet imported{};
	bool succeeded = false;
	try {

		imported = ImportAnimationFile(job.meshAssetID, job.fullPath);
		succeeded = imported.valid;
	}
	catch (...) {
		succeeded = false;
	}
	// 結果の保存
	{
		std::scoped_lock lock(mutex_);
		loading_.erase(job.meshAssetID);
		if (succeeded) {
			loaded_[job.meshAssetID] = std::move(imported);
		}
	}
}

Engine::SkinnedMeshAnimationSet Engine::SkinnedMeshAnimationManager::ImportAnimationFile(
	AssetID meshAssetID, const std::filesystem::path& fullPath) const {

	SkinnedMeshAnimationSet result{};
	result.meshAssetID = meshAssetID;

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fullPath.string(), 0);
	if (!scene || !scene->mRootNode) {
		return result;
	}

	// ノード階層を再帰的に読み込んでスケルトンを構築
	MeshNode rootNode = ReadNodeForAnimation(scene->mRootNode);
	result.skeleton = BuildSkeleton(rootNode);

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
			auto it = result.skeleton.jointMap.find(bone->mName.C_Str());
			if (it == result.skeleton.jointMap.end()) {
				continue;
			}

			const int32_t jointIndex = it->second;
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
		bool vaildDuration = 0.0 < anim->mTicksPerSecond;

		// 再生時間
		clip.duration = vaildDuration ? static_cast<float>(anim->mDuration / anim->mTicksPerSecond) : static_cast<float>(anim->mDuration);

		for (uint32_t c = 0; c < anim->mNumChannels; ++c) {

			const aiNodeAnim* nodeAnim = anim->mChannels[c];
			if (!nodeAnim) {
				continue;
			}

			NodeAnimation& dst = clip.nodeAnimations[nodeAnim->mNodeName.C_Str()];
			// キーフレーム構築
			// 座標
			for (uint32_t k = 0; k < nodeAnim->mNumPositionKeys; ++k) {

				const aiVectorKey& kv = nodeAnim->mPositionKeys[k];
				KeyframeVector3 f{};
				f.time = vaildDuration ? static_cast<float>(kv.mTime / anim->mTicksPerSecond) : static_cast<float>(kv.mTime);
				f.value = { -kv.mValue.x, kv.mValue.y, kv.mValue.z };
				dst.translate.keyframes.emplace_back(f);
			}
			// 回転
			for (uint32_t k = 0; k < nodeAnim->mNumRotationKeys; ++k) {

				const aiQuatKey& kv = nodeAnim->mRotationKeys[k];
				KeyframeQuaternion f{};
				f.time = vaildDuration ? static_cast<float>(kv.mTime / anim->mTicksPerSecond) : static_cast<float>(kv.mTime);
				f.value = { kv.mValue.x, -kv.mValue.y, -kv.mValue.z, kv.mValue.w };
				dst.rotate.keyframes.emplace_back(f);
			}
			// スケール
			for (uint32_t k = 0; k < nodeAnim->mNumScalingKeys; ++k) {

				const aiVectorKey& kv = nodeAnim->mScalingKeys[k];
				KeyframeVector3 f{};
				f.time = vaildDuration ? static_cast<float>(kv.mTime / anim->mTicksPerSecond) : static_cast<float>(kv.mTime);
				f.value = { kv.mValue.x, kv.mValue.y, kv.mValue.z };
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

	// アニメーションクリップの名前 -> ジョイントインデックスに対応したNodeAnimation*配列を構築
	result.clipJointTracks.reserve(result.clips.size());
	for (auto& [clipName, clip] : result.clips) {

		std::vector<const NodeAnimation*>& tracks = result.clipJointTracks[clipName];
		tracks.assign(result.skeleton.joints.size(), nullptr);
		for (const Joint& joint : result.skeleton.joints) {

			auto it = clip.nodeAnimations.find(joint.name);
			if (it != clip.nodeAnimations.end()) {
				tracks[joint.index] = &it->second;
			}
		}
	}

	// スケルトンとアニメーションクリップの両方が存在する場合、有効なアニメーションセットとする
	result.valid = !result.skeleton.joints.empty() && !result.clips.empty();
	return result;
}