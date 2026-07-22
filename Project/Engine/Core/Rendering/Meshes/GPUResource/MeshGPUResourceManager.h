#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Meshes/MeshImportService.h>

// c++
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace Engine {

	//============================================================================
	//	MeshGPUResourceManager class
	//	メッシュアセットのGPUリソースを管理するクラス
	//============================================================================
	class MeshGPUResourceManager {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MeshGPUResourceManager() = default;
		~MeshGPUResourceManager();

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// 前フレーム処理
		void BeginFrame(GraphicsCore& graphicsCore);
		// 指定メッシュIDの読み込みを要求
		void RequestMesh(AssetDatabase& assetDatabase, AssetID meshAssetID);
		// 既にロード済みのメッシュを破棄して再インポートを要求する、外部編集のホットリロード用で未ロードは無視する
		void RequestReload(AssetID meshAssetID);
		// 読み込み待ちのメッシュアセットがあればGPUにアップロードする
		void FlushUploads();
		// 要求した全メッシュの読み込みとGPUリソース作成を完了する
		void WaitAll();

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		// メッシュアセットのGPUリソースを取得
		const MeshGPUResource* Find(AssetID meshAssetID) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;
		BufferUploadService* uploadService_ = nullptr;

		AssetDatabase* assetDatabase_ = nullptr;

		// 読み込み、提供するメッシュアセットのインポートサービス
		MeshImportService importService_{};
		bool initialized_ = false;

		// アセットIDとGPUリソースのマップ
		mutable std::mutex mutex_;
		std::unordered_map<AssetID, MeshGPUResource> gpuMeshes_;
		std::unordered_set<AssetID> requested_;
		// メッシュごとのホットリロード世代、リロード要求のたびに増やす
		std::unordered_map<AssetID, uint32_t> reloadGeneration_;

		//--------- functions ----------------------------------------------------

		void UploadImported(const ImportedMeshAsset& imported);
		// メッシュGPUリソースが持つ全SRVを解放する、破棄と再ロードで共用する
		void ReleaseMeshResource(MeshGPUResource& mesh);
	};
} // Engine

