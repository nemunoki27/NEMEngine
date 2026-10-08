#pragma once

//============================================================================
//	include
//============================================================================
#include "FontSourceSnapshot.h"
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>

// c++
#include <map>
#include <memory>
#include <unordered_map>

namespace Engine {

	class RenderAssetLibrary;
	class TextureUploadService;

	// 描画中だけ借用するFontとAtlas
	struct FontRenderGeneration {

		const MSDFFontAsset* font = nullptr;
		const GPUTextureResource* atlas = nullptr;
	};

	//============================================================================
	//	FontRenderService class
	//	FontとAtlasの描画世代を成功後に公開する
	//============================================================================
	class FontRenderService {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit FontRenderService(TextureUploadService& textures);
		~FontRenderService();
		FontRenderService(const FontRenderService&) = delete;
		FontRenderService& operator=(const FontRenderService&) = delete;

		// 新しい世代の準備中は公開済みの一組を返す
		FontRenderGeneration Resolve(RenderAssetLibrary& library, AssetID assetID);
		// 終了したLibraryの固定画像を解放する
		void CollectExpired();
		// Textureサービスの終了前に固定画像を解放する
		void Clear();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 一つのFontの要求と公開済み世代
		struct Entry {

			std::shared_ptr<const FontSourceSnapshot> requested;
			std::shared_ptr<const MSDFFontAsset> published;
			std::string pendingKey;
			std::string publishedKey;
			uint64_t atlasReloadRevision = 0;
		};

		using LibraryIdentity = std::weak_ptr<const uint64_t>;
		using Entries = std::unordered_map<AssetID, Entry>;

		//--------- variables ----------------------------------------------------

		// Graphicsが本サービスより長く保持する転送窓口
		TextureUploadService& textures_;
		// 別Libraryの同じ内容番号も描画側では区別する
		uint64_t nextContentRevision_ = 1;
		std::map<LibraryIdentity, Entries, std::owner_less<LibraryIdentity>> libraries_;

		//--------- functions ----------------------------------------------------

		// 準備と公開に使用した固定画像を解放する
		void Release(Entries& entries);
	};
} // namespace Engine
