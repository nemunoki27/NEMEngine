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
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

	// 前方宣言
	class AssetDatabase;

	//============================================================================
	//	SkinnedMeshAnimationManager structures
	//============================================================================
	// スキンメッシュアニメーションセット
	struct SkinnedMeshAnimationSet {

		// 公開前の定義はTrackの参照先と一緒に移動する
		SkinnedMeshAnimationSet() = default;
		SkinnedMeshAnimationSet(const SkinnedMeshAnimationSet&) = delete;
		SkinnedMeshAnimationSet& operator=(const SkinnedMeshAnimationSet&) = delete;
		SkinnedMeshAnimationSet(SkinnedMeshAnimationSet&&) = default;
		SkinnedMeshAnimationSet& operator=(SkinnedMeshAnimationSet&&) = default;

		AssetID meshAssetID{};	 // 読込元のMesh
		bool valid = false;		 // 使用できる骨格とClipの有無
		uint64_t generation = 0; // 公開した定義の世代

		// バインド時の骨格
		Skeleton skeleton{};
		// Jointに対応する逆バインド行列
		SkinCluster skinCluster{};

		// アニメーションクリップの名前配列
		std::vector<std::string> clipOrder{};

		// 名前別のアニメーションClip
		std::unordered_map<std::string, AnimationData> clips{};

		// Clip別のJointに対応するTrack参照
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

		// 完成した定義の世代を共有して取得する
		std::shared_ptr<const SkinnedMeshAnimationSet> Find(AssetID meshAssetID) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// アニメーションセットの読み込みジョブ
		struct LoadJob {

			AssetID meshAssetID{};			  // 読込元のMesh
			std::filesystem::path fullPath{}; // 読込元の絶対パス
			uint64_t serial = 0;			  // 最新の読込要求番号
		};

		// 取得済みの内容とパスの更新状態
		struct RequestState {

			uint64_t contentRevision = UINT64_MAX;	 // 要求した内容の世代
			uint64_t structureRevision = UINT64_MAX; // パスを解決した索引の世代
			std::filesystem::path fullPath;			 // 要求した読込元のパス
			uint64_t serial = 0; // 最新の読込要求番号
		};

		//--------- variables ----------------------------------------------------

		// 要求と公開世代の排他
		mutable std::mutex mutex_{};
		// 骨格とClipを読み込むworker
		AssetWorkerPool<LoadJob> workerPool_{};

		// 読み込まれたアニメーションセットのマップ
		std::unordered_map<AssetID, std::shared_ptr<const SkinnedMeshAnimationSet>> loaded_{};

		// 最新要求だけを公開し、失敗した内容は更新まで再試行しない
		std::unordered_map<AssetID, RequestState> requests_;
		// 次の読込要求番号
		uint64_t nextSerial_ = 1;
		// 次に公開する定義の世代
		uint64_t nextGeneration_ = 1;

		//--------- functions ----------------------------------------------------

		// アニメーションセットの読み込みジョブ
		void LoadJobAsync(LoadJob&& job, uint32_t workerIndex);
		// アニメーションファイルのインポート
		SkinnedMeshAnimationSet ImportAnimationFile(AssetID meshAssetID, const std::filesystem::path& fullPath) const;
	};
} // Engine
