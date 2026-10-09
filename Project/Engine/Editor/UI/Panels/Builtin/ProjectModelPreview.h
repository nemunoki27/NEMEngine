#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolRenderResources.h>
#include <Engine/Editor/Assets/Project/ProjectAssetIndex.h>

// c++
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ProjectModelPreview class
	//	非同期読込の結果からモデルのサムネイルを保持する
	//============================================================================
	class ProjectModelPreview {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 現在フォルダーの未描画モデルを順に更新する
		void PrepareModelPreviews(const EditorPanelContext& context, AssetDatabase& database);
		// 表示範囲に入ったモデルだけを要求する
		void RequestVisibleMesh(AssetID asset);
		// 作成済みのサムネイルを取得する
		bool TryGetModelPreviewImage(AssetID assetID, ImTextureID& outTextureID, ImVec2& outUV0, ImVec2& outUV1) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct PreviewEntry {

			Entity entity = Entity::Null();
			uint32_t meshGeneration = 0;
			uint64_t textureRevision = 0;
			uint64_t lastUsedFrame = 0;
			bool rendered = false;
		};

		//--------- variables ----------------------------------------------------

		// 描画資源は既存の回収窓口で保持する
		EditorToolRenderResources resources_;
		std::unique_ptr<ECSWorld> world_;
		std::unordered_map<AssetID, PreviewEntry> entries_;
		std::weak_ptr<const uint8_t> databaseLifetime_;
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
		// モデルごとの描画資源名を作る
		static std::string MakeTextureName(AssetID asset);
	};
}
