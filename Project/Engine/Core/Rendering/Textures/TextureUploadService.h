#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxUploadContext.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>
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
	// テクスチャのアップロード要求を表す構造体
	struct TextureFileRequestDesc {

		std::string key;
		std::string assetPath;

		// .metaから解決した取り込み設定
		TextureImportSettings importSettings{};
		// 描画用途から要求する色空間、.metaの明示色空間が優先される
		TextureColorSpace requestedColorSpace = TextureColorSpace::Auto;
		// InspectorプレビューでImporter設定より表示色空間を優先する
		bool overrideImportColorSpace = false;
		// エディタプレビュー用のチャンネル変換
		TexturePreviewChannel previewChannel = TexturePreviewChannel::Color;
		// ホットリロードでの再アップロードか、trueなら既存SRVインデックスへ上書きする
		bool reload = false;
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
			// ホットリロードでの再アップロードか
			bool reload = false;
		};

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		ID3D12CommandQueue* graphicsQueue_ = nullptr;
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
		std::unordered_set<std::string> deferredReloadKeys_;
		// ファイル由来テクスチャの再デコードに使う元リクエスト
		std::unordered_map<std::string, TextureFileRequestDesc> keyRequests_;

		//--------- functions ----------------------------------------------------

		// アップロードジョブの記録
		void DecodeTextureWorker(TextureFileRequestDesc&& job, uint32_t workerIndex);
		// アップロードジョブの処理
		GPUTextureResource UploadSolidColor1x1(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
		GPUTextureResource UploadScratchImage(const DirectX::ScratchImage& image, const DirectX::TexMetadata& meta,
			uint32_t reuseSrvIndex = UINT32_MAX);
	};
} // Engine

