#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <string>
#include <string_view>

namespace Engine {

	struct ToolContext;
	struct EditorToolContext;

	//============================================================================
	//	RenderFeatureEditSession class
	//	Profileの選択と設定取込と保存要求を管理するクラス
	//============================================================================
	class RenderFeatureEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 切替要求を反映する
		bool Tick(ToolContext& context);
		// 編集した構成を描画側のプレビューへ渡す
		void SynchronizePreview(const EditorToolContext& context);
		// 次の更新で切り替えるAssetを予約する
		void RequestProfile(AssetID assetID) { requestedProfile_ = assetID; }
		// 保存確認後に編集対象を切り替える
		void SelectProfile(const EditorToolContext& context, AssetID profileAsset);
		// 別AssetのPass構成を取り込む
		bool ImportProfileSettings(const EditorToolContext& context, AssetID sourceProfile);
		// 編集した構成を保存する
		void Save();
		// 保存済みの構成へ戻す
		void Reload();
		// 編集状態と実行構成を更新する
		void SetDirty();
		// 操作結果を表示用に保持する
		void SetStatusMessage(const std::string& message, bool error);
		// Pass用Materialへ変換できるAssetか判定する
		static bool IsPassMaterialSource(AssetType assetType, std::string_view assetPath);
		// MaterialまたはCompute ShaderからPass用Materialを解決する
		AssetID ResolvePassMaterial(
			const EditorToolContext& context, AssetID assetID, AssetType assetType, std::string_view assetPath);

		//--------- accessor -----------------------------------------------------

		AssetID GetProfileID() const { return observedProfile_; }
		const std::string& GetStatusMessage() const { return statusMessage_; }
		bool HasError() const { return statusError_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID requestedProfile_{};	 // 次の更新で切り替えるAsset
		AssetID observedProfile_{};		 // 編集中のAsset
		AssetID previewAsset_{};		 // 描画側へ公開済みのAsset
		uint64_t previewGeneration_ = 0; // 公開済み構成の世代
		std::string statusMessage_{};	 // 操作結果の表示文
		bool statusError_ = false;		 // 操作結果の失敗状態
	};
}
