#pragma once

//============================================================================
//	include
//============================================================================
#include "InspectorMaterialPreview.h"


// c++
#include <unordered_map>
#include <utility>

namespace Engine {

	class TextureUploadService;

	//============================================================================
	//	ProjectAssetPreview class
	//	モデルとマテリアルのサムネイルをパネル間で共有する
	//============================================================================
	class ProjectAssetPreview {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 現在フォルダーの未描画アセットを順に更新する
		void Prepare(const EditorPanelContext& context, AssetDatabase& database);
		// 表示範囲に入ったアセットだけを要求する
		void Request(AssetID asset);
		const char* GetFallbackLabel(AssetID asset) const;
		// 作成済みのサムネイルを取得する
		bool TryGetImage(AssetID assetID, ImTextureID& outTextureID, ImVec2& outUV0, ImVec2& outUV1) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct PreviewEntry {

			Entity entity = Entity::Null();
			uint32_t meshGeneration = 0;
			const char* fallback = nullptr;
			uint64_t retryFrame = 0;
			uint64_t databaseRevision = 0;
			std::vector<std::pair<AssetID, uint64_t>> dependencies;
			uint64_t textureRevision = 0;
			uint64_t lastUsedFrame = 0;
			bool rendered = false;
			std::vector<std::pair<std::string, std::weak_ptr<const uint8_t>>> textures;
		};

		//--------- variables ----------------------------------------------------

		// 描画資源は既存の回収窓口で保持する
		EditorToolRenderResources resources_;
		InspectorMaterialPreview materialPreview_;
		std::unique_ptr<ECSWorld> world_;
		std::unordered_map<AssetID, PreviewEntry> entries_;
		std::weak_ptr<const uint8_t> databaseLifetime_;
		Color4 thumbnailBackground_{};
		uint64_t frame_ = 0;
		uint64_t observedTextureRevision_ = 0;
		uint32_t textureQuietFrames_ = 0;
		size_t nextEntry_ = 0;
		std::vector<AssetID> visibleAssets_;

		//--------- functions ----------------------------------------------------

		// Project切替時に古いサムネイルを解放する
		void Reset(const AssetDatabase& database);
		// 上限を超えた古いサムネイルを解放する
		void TrimCache();
		// アセットごとの描画資源名を作る
		static std::string MakeTextureName(AssetID asset);
		static void CollectMaterialDependencies(PreviewEntry& entry, const AssetDatabase& database, AssetID asset);
		// 依存画像の差替えだけをサムネイルへ反映する
		static bool UpdateTextureReferences(PreviewEntry& entry, TextureUploadService& uploads, bool update = true);
	};
}
