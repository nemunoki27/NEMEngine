#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Meshes/MeshImportService.h>

// c++
#include <unordered_map>
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
		// Deviceと転送・回収先を指定して初期化する
		void Init(ID3D12Device* device, BufferUploadService& uploads, SRVDescriptor& descriptors);

		// 前フレーム処理
		void BeginFrame(GraphicsCore& graphicsCore);
		// 指定メッシュIDの読み込みを要求
		void RequestMesh(AssetDatabase& assetDatabase, AssetID meshAssetID);
		// 旧Meshを保持して再インポートし、成功後に差し替える
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
		// GPUメッシュの追加、再読込、破棄で進む世代
		uint64_t GetResourceRevision() const { return resourceRevision_; }
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
		// 実行中の要求世代と、最後に受け付けた要求世代
		std::unordered_map<AssetID, uint64_t> requested_;
		std::unordered_map<AssetID, uint64_t> requestRevisions_;
		// 再初期化を越えて保持するMeshの公開世代
		std::unordered_map<AssetID, uint32_t> reloadGeneration_;
		uint64_t resourceRevision_ = 1;

		//--------- functions ----------------------------------------------------

		// 要求世代が一致したMeshだけGPUへ公開する
		void UploadImported(const ImportedMeshAsset& imported, uint64_t revision);
		// 保護中の要求世代を進め、未実行なら読込を開始する
		void BeginRequest(AssetID asset);
		// 保護中の最新要求を読み込みへ渡す
		void QueueLatestRequest(AssetID asset, uint64_t revision);
		// 完了した要求を外し、新しい要求があれば再投入する
		void CompleteRequest(AssetID asset, uint64_t revision);
		// メッシュGPUリソースが持つ全SRVを解放する、破棄と再ロードで共用する
		void ReleaseMeshResource(MeshGPUResource& mesh);
	};
} // Engine

