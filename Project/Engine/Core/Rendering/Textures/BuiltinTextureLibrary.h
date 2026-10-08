#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>

namespace Engine {

	// 前方宣言
	class TextureUploadService;

	//============================================================================
	//	BuiltinTextureLibrary class
	//	エンジン組み込みのテクスチャを管理するクラス
	//============================================================================
	class BuiltinTextureLibrary {
	public:
		// 定数

		static constexpr const char* kErrorTextureKey = "builtin:error1x1";
		static constexpr const char* kWhiteTextureKey = "builtin:white1x1";
		static constexpr const char* kNeutralDisplacementTextureKey = "builtin:neutralDisplacement1x1";

		//============================================================================
		//	public Methods
		//============================================================================

		BuiltinTextureLibrary() = default;
		~BuiltinTextureLibrary() = default;

		// 初期化
		void Init(TextureUploadService& uploadService);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		// 白テクスチャを借用し初期化前は未取得を返す
		const GPUTextureResource* GetWhiteTexture() const;
		// 変位なしのテクスチャを借用する
		const GPUTextureResource* GetNeutralDisplacementTexture() const;
		// 読込失敗時のテクスチャを借用する
		const GPUTextureResource* GetErrorTexture() const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 初期化から終了まで借用する転送サービス
		TextureUploadService* uploadService_ = nullptr;
	};
} // Engine
