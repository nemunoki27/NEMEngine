#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/Asset/Async/AssetWorkerPool.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Graphics/Mesh/MeshNode.h>

// c++
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <filesystem>
// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace Engine {

	//============================================================================
	//	MeshImportService class
	//	メッシュのインポート処理を行うクラス
	//============================================================================
	class MeshImportService {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MeshImportService() = default;
		~MeshImportService() = default;

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
		// メッシュアセットが読み込まれているか
		bool IsLoaded(AssetID meshAssetID) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// メッシュの読み込みジョブ
		struct MeshLoadJob {

			AssetID assetID{};
			std::filesystem::path fullPath;
		};

		//--------- variables ----------------------------------------------------

		mutable std::mutex mutex_;
		AssetWorkerPool<MeshLoadJob> workerPool_;

		// 読み込まれたメッシュアセットのマップ
		std::unordered_map<AssetID, ImportedMeshAsset> imported_;

		// 読み込み待ちと読み込み中のアセットIDのセット
		std::unordered_set<AssetID> queued_;
		std::unordered_set<AssetID> loading_;

		//--------- functions ----------------------------------------------------

		void LoadJob(MeshLoadJob&& job, uint32_t workerIndex);
		ImportedMeshAsset ImportFile(AssetID assetID, const std::filesystem::path& fullPath) const;
		MeshNode ReadNode(aiNode* node) const;
	};
} // Engine
