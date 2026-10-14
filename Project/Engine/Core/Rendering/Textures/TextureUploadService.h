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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include <vector>
// directX
#include <DirectXTex.h>
#include <d3dx12.h>
#include <dxgi1_6.h>

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

	struct TextureCacheStatistics {

		uint64_t textureCount = 0;
		uint64_t allocationBytes = 0;
		uint64_t queuedCount = 0;
		uint64_t decodedBytes = 0;
		uint64_t uploadingCount = 0;
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
		~TextureUploadService();

		// 初期化
		void Init(ID3D12Device* device, SRVDescriptor* srvDescriptor);

		// 毎フレーム主スレッド更新
		void TickFinalize();
		// 遅延再読込を含む全デコードとGPU転送の完了を待つ
		void WaitAll();

		// アップロード要求
		void RequestSolidColor1x1(const std::string& key, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
		void RequestTextureFile(const TextureFileRequestDesc& desc);
		void RequestTextureFile(const std::string& key, const std::string& assetPath);
		// 固定した画像を専用キーで要求する
		std::string RequestSnapshot(TextureFileRequestDesc description);
		// 固定画像の所有を解放し、転送中の結果も公開しない
		void ReleaseSnapshot(const std::string& key);

		// 最新要求で再読込し、成功後にResourceとDescriptorを差し替える
		void RequestReload(const std::string& key);
		// 指定ファイルを指す全てのキー(描画用base/sRGBやProjectPanelサムネイル等)をまとめて再ロードする
		void RequestReloadByFile(const std::filesystem::path& fullPath,
			const TextureImportSettings* updatedSettings = nullptr);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		const GPUTextureResource* GetTexture(const std::string& key) const;
		TextureRequestState GetState(const std::string& key) const;
		// デコードとGPU公開が完了していない要求を確認する
		bool HasPendingRequests() const;
		// 公開資源と転送待ちの使用量を取得する
		TextureCacheStatistics GetCacheStatistics() const;
		uint64_t GetContentRevision() const { return contentRevision_.load(std::memory_order_relaxed); }
		// 指定ファイルの再読込要求の世代を取得する
		uint64_t GetFileReloadRevision(const std::filesystem::path& fullPath) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// ファイル要求と受付時の世代
		struct DecodeRequest {

			TextureFileRequestDesc description;
			uint64_t revision = 0;
		};

		// 読込結果と元の要求世代
		struct CompletedRequest {

			DecodedTexture texture;
			uint64_t revision = 0;
		};

		// 転送Fenceの完了まで公開を保留する候補
		struct PendingGPURequest {

			CompletedRequest completed;
			GPUTextureResource uploaded;
		};

		//--------- variables ----------------------------------------------------

		std::atomic<uint64_t> contentRevision_ = 0;
		uint64_t nextSnapshotID_ = 1;
		uint64_t nextFileReloadRevision_ = 1;
		// 固定画像の所有元へ新しいファイル要求を伝える
		std::unordered_map<std::wstring, uint64_t> fileReloadRevisions_;
		SRVDescriptor* srvDescriptor_ = nullptr;
		TextureGPUUploader uploader_;

		// 記録されたアップロードジョブ
		AssetWorkerPool<DecodeRequest> decodeWorkers_;

		// アップロードジョブのキューと完了したテクスチャのマップを保護するミューテックス
		mutable std::mutex mutex_;
		std::condition_variable decodedSpace_;
		bool acceptingDecodes_ = false;
		std::deque<CompletedRequest> pendingUploads_;
		std::deque<PendingGPURequest> pendingGPUUploads_;
		std::atomic<uint64_t> pendingGPUCount_ = 0;
		uint64_t decodedBytes_ = 0;
		uint64_t readyAllocationBytes_ = 0;
		// キーとGPUテクスチャリソースのマップ
		std::unordered_map<std::string, GPUTextureResource> readyTextures_;
		mutable std::unordered_map<std::string, std::chrono::steady_clock::time_point> textureAccessTimes_;
		ComPtr<IDXGIAdapter3> cacheAdapter_;
		std::chrono::steady_clock::time_point cacheCollectionTime_{};
		std::unordered_set<std::string> queuedKeys_;
		std::unordered_set<std::string> failedKeys_;
		std::unordered_set<std::string> deferredReloadKeys_;
		// ファイル由来テクスチャの再デコードに使う元リクエスト
		std::unordered_map<std::string, DecodeRequest> keyRequests_;

		//--------- functions ----------------------------------------------------

		// アップロードジョブの記録
		void DecodeTextureWorker(DecodeRequest&& request, uint32_t workerIndex);
		// ファイル比較の区切りと大小文字を揃える
		static std::wstring NormalizeFilePath(const std::filesystem::path& path);
		// デコード要求を投入し、受付失敗を状態へ戻す
		bool QueueDecode(const DecodeRequest& request);
		// 保護中の要求を更新し、進行中なら再投入を予約する
		void PrepareRequest(DecodeRequest& request, std::vector<DecodeRequest>& toEnqueue);
		// 保護中の要求と完了結果の世代を照合する
		bool IsCurrentRequest(const CompletedRequest& completed) const;
		// 転送済みの候補を要求世代と照合して公開する
		void PublishUpload(const CompletedRequest& completed, GPUTextureResource& uploaded);
		// 常駐予算と未使用画像の所有を確認する
		void InitCacheTracking(ID3D12Device* device);
		void CollectUnusedTextures();
	};
} // Engine
