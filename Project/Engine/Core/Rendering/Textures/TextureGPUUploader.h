#pragma once

//============================================================================
//	include
//============================================================================
#include "GPUTextureResource.h"
#include <Engine/Core/Rendering/DxObject/Core/DxUploadContext.h>

#include <memory>
#include <DirectXTex.h>

namespace Engine {

	class SRVDescriptor;

	//============================================================================
	//	TextureGPUUploader class
	//	Texture生成と転送コマンドを所有する
	//============================================================================
	class TextureGPUUploader {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void Init(ID3D12Device* device, SRVDescriptor* descriptor);
		void Finalize();
		// 待機を省いた候補は転送完了後に公開する
		GPUTextureResource UploadSolidColor1x1(uint8_t r, uint8_t g, uint8_t b, uint8_t a, bool deferCompletion = false);
		GPUTextureResource UploadScratchImage(const DirectX::ScratchImage& image, const DirectX::TexMetadata& meta,
			bool deferCompletion = false);
		// 記録済みの画像をまとめて提出する
		void SubmitBatch();
		// 完了済みBatchだけを確定する
		bool TryFinalize();
		// 明示的な読込待機と終了時に転送を確定する
		void FlushAndWait();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;
		std::unique_ptr<DxUploadCommand> uploadCommand_;
		bool hasRecorded_ = false;
	};
}
