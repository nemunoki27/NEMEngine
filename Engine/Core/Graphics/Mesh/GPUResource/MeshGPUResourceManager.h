#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Mesh/MeshImportService.h>

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
		//========================================================================
		//	public Methods
		//========================================================================

		MeshGPUResourceManager() = default;
		~MeshGPUResourceManager();

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// 前フレーム処理
		void BeginFrame(GraphicsCore& graphicsCore);
		// 指定メッシュIDの読み込みを要求
		void RequestMesh(AssetDatabase& assetDatabase, AssetID meshAssetID);
		// 読み込み待ちのメッシュアセットがあればGPUにアップロードする
		void FlushUploads();

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		// メッシュアセットのGPUリソースを取得
		const MeshGPUResource* Find(AssetID meshAssetID) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;

		AssetDatabase* assetDatabase_ = nullptr;

		// 読み込み、提供するメッシュアセットのインポートサービス
		MeshImportService importService_{};
		bool initialized_ = false;

		// アセットIDとGPUリソースのマップ
		mutable std::mutex mutex_;
		std::unordered_map<AssetID, MeshGPUResource> gpuMeshes_;
		std::unordered_set<AssetID> requested_;

		//--------- functions ----------------------------------------------------

		void UploadImported(const ImportedMeshAsset& imported);
	};
} // Engine