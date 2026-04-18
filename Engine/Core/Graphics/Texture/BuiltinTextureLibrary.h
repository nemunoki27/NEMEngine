#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Texture/GPUTextureResource.h>

// c++
#include <string>

namespace Engine {

	// front
	class TextureUploadService;

	//============================================================================
	//	BuiltinTextureLibrary class
	//	エンジン組み込みのテクスチャを管理するクラス
	//============================================================================
	class BuiltinTextureLibrary {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BuiltinTextureLibrary() = default;
		~BuiltinTextureLibrary() = default;

		// 初期化
		void Init(TextureUploadService& uploadService);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		const GPUTextureResource* GetWhiteTexture() const;
		const GPUTextureResource* GetErrorTexture() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		TextureUploadService* uploadService_ = nullptr;

		// 1x1の白テクスチャ
		std::string whiteKey_ = "builtin:white1x1";
		// エラーテクスチャ
		std::string errorKey_ = "builtin:error1x1";
	};
} // Engine