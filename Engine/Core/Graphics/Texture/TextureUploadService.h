#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxObject/DxUploadCommand.h>
#include <Engine/Core/Graphics/Texture/GPUTextureResource.h>
#include <Engine/Core/Asset/Async/AssetWorkerPool.h>

// c++
#include <unordered_map>
#include <unordered_set>
// directX
#include <Externals/DirectXTex/DirectXTex.h>
#include <Externals/DirectXTex/d3dx12.h>

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
	// テクスチャのアップロード要求を表す構造体
	struct TextureFileRequestDesc {

		std::string key;
		std::string assetPath;

		// WICロード時にsRGBとして扱うか
		bool forceSRGB = false;
	};

	//============================================================================
	//	TextureUploadService class
	//	テクスチャのアップロードを管理して提供するサービスクラス
	//============================================================================
	class TextureUploadService {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		TextureUploadService() = default;
		~TextureUploadService() = default;

		// 初期化
		void Init(ID3D12Device* device, SRVDescriptor* srvDescriptor);

		// 毎フレーム主スレッド更新
		void TickFinalize();

		// アップロード要求
		void RequestSolidColor1x1(const std::string& key, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
		void RequestTextureFile(const TextureFileRequestDesc& desc);
		void RequestTextureFile(const std::string& key, const std::string& assetPath);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		const GPUTextureResource* GetTexture(const std::string& key) const;
		TextureRequestState GetState(const std::string& key) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// アップロード待ちのジョブを表す構造体
		struct PendingUploadJob {

			// テクスチャキー
			std::string key;

			// 単色設定
			bool isSolidColor = false;
			uint8_t solidRGBA[4]{};
			// アップロードするテクスチャデータ
			DirectX::ScratchImage image;
			DirectX::TexMetadata metadata{};

			// アップロードの成功フラグ
			bool success = false;
		};

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;
		std::unique_ptr<DxUploadCommand> uploadCommand_;

		// 記録されたアップロードジョブ
		AssetWorkerPool<TextureFileRequestDesc> decodeWorkers_;

		// アップロードジョブのキューと完了したテクスチャのマップを保護するミューテックス
		mutable std::mutex mutex_;
		std::deque<PendingUploadJob> pendingUploads_;
		// キーとGPUテクスチャリソースのマップ
		std::unordered_map<std::string, GPUTextureResource> readyTextures_;
		std::unordered_set<std::string> queuedKeys_;
		std::unordered_set<std::string> failedKeys_;

		//--------- functions ----------------------------------------------------

		// アップロードジョブの記録
		void DecodeTextureWorker(TextureFileRequestDesc&& job, uint32_t workerIndex);
		// アップロードジョブの処理
		GPUTextureResource UploadSolidColor1x1(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
		GPUTextureResource UploadScratchImage(const DirectX::ScratchImage& image, const DirectX::TexMetadata& meta);
	};
} // Engine