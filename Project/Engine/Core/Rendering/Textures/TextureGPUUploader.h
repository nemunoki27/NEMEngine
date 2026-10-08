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
		GPUTextureResource UploadSolidColor1x1(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
		GPUTextureResource UploadScratchImage(const DirectX::ScratchImage& image, const DirectX::TexMetadata& meta);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ID3D12Device* device_ = nullptr;
		SRVDescriptor* srvDescriptor_ = nullptr;
		std::unique_ptr<DxUploadCommand> uploadCommand_;
	};
}
