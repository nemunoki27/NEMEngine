#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportSettings.h>

// c++
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <filesystem>

namespace Engine {

	class AssetDatabase;

	//============================================================================
	//	MeshImportService class
	//	モデル読込の要求とworkerの状態を管理する
	//============================================================================
	class MeshImportService {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MeshImportService() = default;
		~MeshImportService();

		// 初期化
		void Init(uint32_t threadCount);

		// メッシュアセットの非同期読み込み要求
		bool RequestLoadAsync(AssetDatabase& assetDatabase, AssetID meshAssetID);
		// 読み込み待ちのジョブがすべて完了するまで待機
		void WaitAll();

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		// 読み込まれたメッシュアセットをムーブで取り出す
		bool TakeImported(AssetID meshAssetID, ImportedMeshAsset& outImported);
		// 読み込み失敗を一度だけ取り出す
		bool ConsumeFailed(AssetID meshAssetID);
		// 読み込み中または待機中か確認する
		bool IsPending(AssetID meshAssetID) const;
		// メッシュアセットが読み込まれているか
		bool IsLoaded(AssetID meshAssetID) const;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// メッシュの読み込みジョブ
		struct MeshLoadJob {

			AssetID assetID{};
			std::filesystem::path fullPath;
			MeshImportSettings settings{};
			std::array<std::filesystem::path, 3> manualLODPaths{};
		};

		//--------- variables ----------------------------------------------------

		// 要求と完了結果の排他
		mutable std::mutex mutex_;
		// 読込処理を実行するworker
		AssetWorkerPool<MeshLoadJob> workerPool_;

		// 読み込まれたメッシュアセットのマップ
		std::unordered_map<AssetID, ImportedMeshAsset> imported_;

		// 読み込み待ちと読み込み中のアセットIDのセット
		std::unordered_set<AssetID> queued_;
		std::unordered_set<AssetID> loading_;
		std::unordered_set<AssetID> failed_;

		//--------- functions ----------------------------------------------------

		// 要求を実行し、成功と失敗を別々に保持する
		void LoadJob(MeshLoadJob&& job);
	};
} // Engine
