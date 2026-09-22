#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxUploadContext.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include "TextureDecoder.h"
#include "TextureGPUUploader.h"
#include <Engine/Core/Assets/Async/AssetWorkerPool.h>

// c++
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
// directX
#include <DirectXTex.h>
#include <d3dx12.h>

namespace Engine {

	// front
	class SRVDescriptor;

	//============================================================================
	//	TextureUploadService structures
	//============================================================================
	// テクスチャのアップロード状態を表す列挙型
	enum class TextureRequestState {

		None,
		Queued,
		Ready,
		Failed,
	};

	//============================================================================
	//	TextureUploadService class
	//	テクスチャのアップロードを管理して提供するサービスクラス
	//============================================================================
	class TextureUploadService {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		TextureUploadService() = default;
		~TextureUploadService() = default;

		// 初期化
		void Init(ID3D12Device* device, SRVDescriptor* srvDescriptor, ID3D12CommandQueue* graphicsQueue);

		// 毎フレーム主スレッド更新
		void TickFinalize();
		// 全デコードとGPU転送の完了を待つ
		void WaitAll();

		// アップロード要求
		void RequestSolidColor1x1(const std::string& key, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
		void RequestTextureFile(const TextureFileRequestDesc& desc);
		void RequestTextureFile(const std::string& key, const std::string& assetPath);

		// 既にロード済みのファイル由来テクスチャを再デコードして同一SRVインデックスへ差し替える、未ロードやsolid colorは無視する
		void RequestReload(const std::string& key);
		// 指定ファイルを指す全てのキー(描画用base/sRGBやProjectPanelサムネイル等)をまとめて再ロードする
		void RequestReloadByFile(const std::filesystem::path& fullPath,
			const TextureImportSettings* updatedSettings = nullptr);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		const GPUTextureResource* GetTexture(const std::string& key) const;
		TextureRequestState GetState(const std::string& key) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		//--------- variables ----------------------------------------------------

		SRVDescriptor* srvDescriptor_ = nullptr;
		TextureGPUUploader uploader_;

		// 記録されたアップロードジョブ
		AssetWorkerPool<TextureFileRequestDesc> decodeWorkers_;

		// アップロードジョブのキューと完了したテクスチャのマップを保護するミューテックス
		mutable std::mutex mutex_;
		std::deque<DecodedTexture> pendingUploads_;
		// キーとGPUテクスチャリソースのマップ
		std::unordered_map<std::string, GPUTextureResource> readyTextures_;
		std::unordered_set<std::string> queuedKeys_;
		std::unordered_set<std::string> failedKeys_;
		std::unordered_set<std::string> deferredReloadKeys_;
		// ファイル由来テクスチャの再デコードに使う元リクエスト
		std::unordered_map<std::string, TextureFileRequestDesc> keyRequests_;

		//--------- functions ----------------------------------------------------

		// アップロードジョブの記録
		void DecodeTextureWorker(TextureFileRequestDesc&& job, uint32_t workerIndex);
	};
} // Engine
