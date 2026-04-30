#include "BuiltinTextureLibrary.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Texture/TextureUploadService.h>
#include <Engine/Logger/Assert.h>

//============================================================================
//	BuiltinTextureLibrary classMethods
//============================================================================

void Engine::BuiltinTextureLibrary::Init(TextureUploadService& uploadService) {

	uploadService_ = &uploadService;

	// 1x1の白テクスチャの作成
	uploadService_->RequestSolidColor1x1(whiteKey_, 255, 255, 255, 255);
	uploadService_->RequestSolidColor1x1(errorKey_, 255, 20, 147, 255);

	// 組み込みテクスチャは描画フォールバックに使うため、起動時に必ずGPUへ反映しておく
	uploadService_->TickFinalize();
	Assert::Call(GetWhiteTexture() != nullptr, "BuiltinTextureLibrary: WhiteTexture の作成に失敗しました");
	Assert::Call(GetErrorTexture() != nullptr, "BuiltinTextureLibrary: ErrorTexture の作成に失敗しました");
}

void Engine::BuiltinTextureLibrary::Finalize() {

	uploadService_ = nullptr;
}

const Engine::GPUTextureResource* Engine::BuiltinTextureLibrary::GetWhiteTexture() const {

	if (!uploadService_) {
		return nullptr;
	}
	return uploadService_->GetTexture(whiteKey_);
}

const Engine::GPUTextureResource* Engine::BuiltinTextureLibrary::GetErrorTexture() const {

	if (!uploadService_) {
		return nullptr;
	}
	return uploadService_->GetTexture(errorKey_);
}
