#include "BuiltinTextureLibrary.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <string>

namespace {

	// 転送APIへ渡す固定キーを共有
	const std::string kWhiteKey = Engine::BuiltinTextureLibrary::kWhiteTextureKey;
	const std::string kNeutralDisplacementKey = Engine::BuiltinTextureLibrary::kNeutralDisplacementTextureKey;
	const std::string kErrorKey = Engine::BuiltinTextureLibrary::kErrorTextureKey;
}

//============================================================================
//	BuiltinTextureLibrary classMethods
//============================================================================
void Engine::BuiltinTextureLibrary::Init(TextureUploadService& uploadService) {

	// 転送サービスを初期化期間中だけ借用
	uploadService_ = &uploadService;

	// 白色と変位なしとエラー用の画像を登録
	uploadService_->RequestSolidColor1x1(kWhiteKey, 255, 255, 255, 255);
	uploadService_->RequestSolidColor1x1(kNeutralDisplacementKey, 128, 128, 128, 255);
	uploadService_->RequestSolidColor1x1(kErrorKey, 255, 20, 147, 255);

	// 描画の代替画像を起動時にGPUへ反映
	uploadService_->TickFinalize();
	Assert::Call(GetWhiteTexture() != nullptr, "BuiltinTextureLibrary: WhiteTexture の作成に失敗しました");
	Assert::Call(GetNeutralDisplacementTexture() != nullptr,
		"BuiltinTextureLibrary: NeutralDisplacementTexture の作成に失敗しました");
	Assert::Call(GetErrorTexture() != nullptr, "BuiltinTextureLibrary: ErrorTexture の作成に失敗しました");
}

void Engine::BuiltinTextureLibrary::Finalize() {

	// 借用を解除し終了後の取得を止める
	uploadService_ = nullptr;
}

const Engine::GPUTextureResource* Engine::BuiltinTextureLibrary::GetWhiteTexture() const {

	// 初期化前と終了後は未取得を返す
	if (!uploadService_) {
		return nullptr;
	}
	return uploadService_->GetTexture(kWhiteKey);
}

const Engine::GPUTextureResource* Engine::BuiltinTextureLibrary::GetNeutralDisplacementTexture() const {

	// 初期化前と終了後は未取得を返す
	if (!uploadService_) {
		return nullptr;
	}
	return uploadService_->GetTexture(kNeutralDisplacementKey);
}

const Engine::GPUTextureResource* Engine::BuiltinTextureLibrary::GetErrorTexture() const {

	// 初期化前と終了後は未取得を返す
	if (!uploadService_) {
		return nullptr;
	}
	return uploadService_->GetTexture(kErrorKey);
}
