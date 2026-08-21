#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <string>
#include <unordered_map>
// imgui
#include <imgui.h>

namespace Engine {

	// front
	class TextureUploadService;
	class AssetDatabase;

	//============================================================================
	//	ProjectAssetThumbnailCache class
	//	プロジェクト内で表示するアセットのサムネイルをキャッシュするクラス
	//============================================================================
	class ProjectAssetThumbnailCache {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ProjectAssetThumbnailCache() = default;
		~ProjectAssetThumbnailCache() = default;

		// 初期化
		void Init(TextureUploadService& textureUploadService);
		// 現在のアセットDBを設定する
		void SetAssetDatabase(const AssetDatabase* assetDatabase);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		// フォルダアイコンのテクスチャIDを取得
		ImTextureID GetFolderIconTextureID() const;
		ImTextureID GetAssetTextureID(const std::string& assetPath, AssetType type);
		// 読み込み済みサムネイルの実ピクセルサイズを取得する
		bool TryGetAssetTextureSize(const std::string& assetPath, Vector2& outSize) const;
		// Pointサンプリングするテクスチャか
		bool UsesNearestSampling(const std::string& assetPath) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct IconEntry {

			std::string textureKey;
			// アセットのフルパス
			std::string assetPath;
		};

		//--------- variables ----------------------------------------------------

		TextureUploadService* textureUploadService_ = nullptr;
		const AssetDatabase* assetDatabase_ = nullptr;

		// 表示するアイコン
		std::unordered_map<AssetType, IconEntry> defaultIcons_;
		std::unordered_map<std::string, IconEntry> customExtensionIcons_;
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
		ImTextureID GetCustomExtensionIcon(const std::string& assetPath) const;
	};
} // Engine
