#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetTypes.h>

// c++
#include <string>
#include <unordered_map>
// imgui
#include <imgui.h>

namespace Engine {

	// front
	class TextureUploadService;

	//============================================================================
	//	ProjectAssetThumbnailCache class
	//	プロジェクト内で表示するアセットのサムネイルをキャッシュするクラス
	//============================================================================
	class ProjectAssetThumbnailCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ProjectAssetThumbnailCache() = default;
		~ProjectAssetThumbnailCache() = default;

		// 初期化
		void Init(TextureUploadService& textureUploadService);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		// フォルダアイコンのテクスチャIDを取得
		ImTextureID GetFolderIconTextureID() const;
		ImTextureID GetAssetTextureID(const std::string& assetPath, AssetType type);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct IconEntry {

			std::string textureKey;
			// アセットのフルパス
			std::string assetPath;
		};

		//--------- variables ----------------------------------------------------

		TextureUploadService* textureUploadService_ = nullptr;

		// 表示するアイコン
		std::unordered_map<AssetType, IconEntry> defaultIcons_;
		std::string folderIconKey_;

		// キャッシュの初期化フラグ
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// キャッシュエントリーを作成する
		void CreateDefaultIcons();

		// パスからサムネイルのキャッシュキーを作成する
		std::string MakeThumbnailKey(const std::string& assetPath) const;
		// ImGuiのテクスチャIDに変換する
		ImTextureID TryGetTextureID(const std::string& key) const;
		ImTextureID GetDefaultTypeIcon(AssetType type) const;
	};
} // Engine