#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Core/IAssetInspectorDrawer.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportSettings.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	MeshAssetInspectorDrawer class
	//	MeshのLOD取り込み設定を編集する
	//============================================================================
	class MeshAssetInspectorDrawer :
		public IAssetInspectorDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AssetType GetAssetType() const override { return AssetType::Mesh; }
		void Draw(const EditorPanelContext& context,
			const AssetMeta& meta) override;
		bool HasPendingChanges() const override {
			return draftSettings_ != savedSettings_;
		}
		bool ApplyPendingChanges(
			const EditorPanelContext& context) override;
		void DiscardPendingChanges() override;
		AssetID GetEditingAsset() const override { return selectedAsset_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID selectedAsset_{};
		MeshImportSettings savedSettings_{};
		MeshImportSettings draftSettings_{};
		std::string statusMessage_{};

		//--------- functions ----------------------------------------------------

		void SyncSelection(const AssetMeta& meta);
		bool ApplySettings(const EditorPanelContext& context,
			const AssetMeta& meta);
	};
} // Engine
