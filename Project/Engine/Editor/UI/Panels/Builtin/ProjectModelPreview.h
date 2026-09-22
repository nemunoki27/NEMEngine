#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolRenderResources.h>
#include <Engine/Editor/Assets/Project/ProjectAssetIndex.h>

#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ProjectModelPreview class
	//	モデル一覧のプレビューと描画資源を所有する
	//============================================================================
	class ProjectModelPreview {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 現在ディレクトリ内のモデルを1枚のRenderTextureへまとめて描画する
		void PrepareModelPreviewAtlas(const EditorPanelContext& context, AssetDatabase& database, const ProjectDirectoryNode& node);
		// AssetIDからモデルプレビューの表示情報を取得する
		bool TryGetModelPreviewImage(AssetID assetID, ImTextureID& outTextureID, ImVec2& outUV0, ImVec2& outUV1) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct ModelPreviewBounds {

			// モデル頂点全体の最小座標
			Vector3 min = Vector3::AnyInit(0.0f);
			// モデル頂点全体の最大座標
			Vector3 max = Vector3::AnyInit(0.0f);
			// プレビューカメラの注視点に使う境界中心
			Vector3 center = Vector3::AnyInit(0.0f);
			// モデル全体を収めるための境界半径
			float radius = 1.0f;
			// 境界計算が成功しているか
			bool valid = false;
		};

		struct ModelPreviewSlot {

			// このスロットに対応するモデルアセットID
			AssetID assetID{};
			// 再構築判定とデバッグ用に保持するモデル仮想パス
			std::string assetPath;
			// プレビュー専用World内で描画するEntity
			Entity entity = Entity::Null();
			// Atlas内の左上ピクセル座標
			Vector2I pixelPos;
			// Atlas内の割り当てサイズ
			Vector2I pixelSize;
			// ImGui::ImageでAtlasから切り出す左上UV
			ImVec2 uv0{};
			// ImGui::ImageでAtlasから切り出す右下UV
			ImVec2 uv1{};
			// スロットごとのカメラ計算に使うモデル境界
			ModelPreviewBounds bounds{};
		};

		struct ModelPreviewAppearanceSettings {

			// 1モデルに割り当てるAtlas上の正方形スロットサイズ
			int32_t tileSize = 192;
			// プレビューAtlasをクリアするときの背景色、暗めの青
			Color4 clearColor = Color4(0.04f, 0.06f, 0.16f, 1.0f);
			// モデルを収めるためのカメラ縦視野角
			float cameraFovY = 35.0f;
			// 境界半径から求めたカメラ距離に掛ける倍率、近づける
			float cameraDistanceScale = 0.8f;
			// 上から見下ろすためのカメラPitch角
			float cameraPitchDegrees = 25.0f;
			// 右寄りから見る3/4ビューにするためのカメラYaw角
			float cameraYawDegrees = 215.0f;
			// プレビューWorldで使うDirectionalLightの向き
			Vector3 lightDirection = Vector3(0.35f, -0.65f, 0.65f);
			// プレビューWorldで使うDirectionalLightの強さ
			float lightIntensity = 1.5f;
		};

		//--------- variables ----------------------------------------------------

		// プレビュー専用の描画資源
		EditorToolRenderResources resources_;

		// モデルプレビューの見た目に関する調整パラメータ、初期値固定で表示する
		ModelPreviewAppearanceSettings modelPreviewSettings_{};
		// モデルプレビュー専用の一時World
		std::unique_ptr<ECSWorld> modelPreviewWorld_;
		// プレビューWorld内のライトEntity
		Entity modelPreviewLightEntity_ = Entity::Null();
		// Atlas内のモデルごとの割り当て情報
		std::vector<ModelPreviewSlot> modelPreviewSlots_;
		// AssetIDからAtlasスロットを引くためのIndex
		std::unordered_map<AssetID, size_t> modelPreviewSlotByAsset_;
		// 現在Atlasを構築しているディレクトリ
		std::string modelPreviewDirectory_;
		// 現在Atlasを構築しているアセット一覧の署名
		uint64_t modelPreviewSignature_ = 0;
		// 1枚だけ持つモデルプレビューAtlasのピクセルサイズ
		Vector2I modelPreviewAtlasSize_;
		// モデル読込完了までAtlasを更新する残りフレーム数
		uint32_t modelPreviewRefreshFrames_ = 0;

		//--------- functions ----------------------------------------------------

		// プレビューWorld内のライトへ現在の表示設定を反映する
		void ApplyModelPreviewLightSettings();
		// モデルプレビューAtlas用の一時Worldとスロットを構築する
		void RebuildModelPreviewSlots(AssetDatabase& database, const ProjectDirectoryNode& node,
			const std::vector<const ProjectAssetEntry*>& meshAssets, uint64_t signature);
		// モデルプレビューAtlasを描画する
		bool RenderModelPreviewAtlas(const EditorToolContext& toolContext, EditorToolRenderTexture& atlas);
		// アセットリストからプレビュー再構築用の署名を作る
		uint64_t BuildModelPreviewSignature(const ProjectDirectoryNode& node,
			const std::vector<const ProjectAssetEntry*>& meshAssets) const;
		// モデル全体を収めるカメラを作るための境界を取得する
		ModelPreviewBounds ComputeModelPreviewBounds(AssetDatabase& database, AssetID meshAssetID) const;
		// モデルの境界に合わせたプレビューカメラを作る
		ManualRenderCameraState BuildModelPreviewCamera(const ModelPreviewBounds& bounds) const;
	};
}
