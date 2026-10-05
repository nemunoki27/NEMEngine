#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <mutex>

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
		uint64_t generation = 0;

		Skeleton skeleton{};
		SkinCluster skinCluster{};

		// アニメーションクリップの名前配列
		std::vector<std::string> clipOrder{};

		// アニメーションクリップの名前->アニメーションデータ
		std::unordered_map<std::string, AnimationData> clips{};

		// クリップ名->ジョイントインデックスに対応したNodeAnimation*配列
		std::unordered_map<std::string, std::vector<const NodeAnimation*>> clipJointTracks{};
	};

	//============================================================================
	//	SkinnedMeshAnimationManager class
	//	骨メッシュのアニメーションを管理するクラス
	//============================================================================
	class SkinnedMeshAnimationManager {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

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

		std::shared_ptr<const SkinnedMeshAnimationSet> Find(AssetID meshAssetID) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// アニメーションセットの読み込みジョブ
		struct LoadJob {

			AssetID meshAssetID{};
			std::filesystem::path fullPath{};
			uint64_t serial = 0;
		};

		struct RequestState {

			uint64_t contentRevision = UINT64_MAX;
			uint64_t structureRevision = UINT64_MAX;
			std::filesystem::path fullPath;
			uint64_t serial = 0;
		};

		//--------- variables ----------------------------------------------------

		mutable std::mutex mutex_{};
		AssetWorkerPool<LoadJob> workerPool_{};

		// 読み込まれたアニメーションセットのマップ
		std::unordered_map<AssetID, std::shared_ptr<const SkinnedMeshAnimationSet>> loaded_{};

		// 最新要求だけを公開し、失敗した内容は更新まで再試行しない
		std::unordered_map<AssetID, RequestState> requests_;
		uint64_t nextSerial_ = 1;
		uint64_t nextGeneration_ = 1;

		//--------- functions ----------------------------------------------------

		// アニメーションセットの読み込みジョブ
		void LoadJobAsync(LoadJob&& job, uint32_t workerIndex);
		// アニメーションファイルのインポート
		SkinnedMeshAnimationSet ImportAnimationFile(AssetID meshAssetID,
			const std::filesystem::path& fullPath) const;
	};
} // Engine
