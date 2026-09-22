#pragma once

//============================================================================
//	include
//============================================================================
#include "TextureImportSettings.h"

#include <string>

namespace Engine {

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

}
