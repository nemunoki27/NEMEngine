#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Asset/Async/AssetWorkerPool.h>
#include <Engine/Core/Asset/AssetTypes.h>

// c++
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <mutex>
// assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace Engine {

	// front
	class AssetDatabase;

	//============================================================================
	//	SkinnedMeshAnimationManager structures
	//============================================================================

	// スキンメッシュアニメーションセット
	struct SkinnedMeshAnimationSet {

		AssetID meshAssetID{};
		bool valid = false;

		Skeleton skeleton{};
		SkinCluster skinCluster{};

		// アニメーションクリップの名前配列
		std::vector<std::string> clipOrder{};

		// アニメーションクリップの名前 -> アニメーションデータ
		std::unordered_map<std::string, AnimationData> clips{};

		// クリップ名 -> ジョイントインデックスに対応したNodeAnimation*配列
		std::unordered_map<std::string, std::vector<const NodeAnimation*>> clipJointTracks{};
	};

	//============================================================================
	//	SkinnedMeshAnimationManager class
	//	骨メッシュのアニメーションを管理するクラス
	//============================================================================
	class SkinnedMeshAnimationManager {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SkinnedMeshAnimationManager() = default;
		~SkinnedMeshAnimationManager();

		// 初期化
		void Init(uint32_t threadCount = 2);

		// アニメーションセットの非同期読み込み要求
		void RequestLoadAsync(AssetDatabase& assetDatabase, AssetID meshAssetID);
		// 読み込み待ちのジョブがすべて完了するまで待機
		void WaitAll();

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		const SkinnedMeshAnimationSet* Find(AssetID meshAssetID) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// アニメーションセットの読み込みジョブ
		struct LoadJob {

			AssetID meshAssetID{};
			std::filesystem::path fullPath{};
		};

		//--------- variables ----------------------------------------------------

		mutable std::mutex mutex_{};
		AssetWorkerPool<LoadJob> workerPool_{};

		// 読み込まれたアニメーションセットのマップ
		std::unordered_map<AssetID, SkinnedMeshAnimationSet> loaded_{};

		// 読み込み待ちと読み込み中のアセットIDのセット
		std::unordered_set<AssetID> queued_{};
		std::unordered_set<AssetID> loading_{};

		//--------- functions ----------------------------------------------------

		// アニメーションセットの読み込みジョブ
		void LoadJobAsync(LoadJob&& job, uint32_t workerIndex);
		// アニメーションファイルのインポート
		SkinnedMeshAnimationSet ImportAnimationFile(AssetID meshAssetID,
			const std::filesystem::path& fullPath) const;
	};
} // Engine