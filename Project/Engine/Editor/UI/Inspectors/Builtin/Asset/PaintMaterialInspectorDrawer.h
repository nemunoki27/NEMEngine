#pragma once

#include <Engine/Editor/UI/Inspectors/Core/IAssetInspectorDrawer.h>
#include <Engine/Editor/UI/Panels/Builtin/InspectorMaterialPreview.h>
#include <Engine/Core/Rendering/Materials/PaintMaterialAsset.h>

namespace Engine {

	class PaintMaterialInspectorDrawer final : public IAssetInspectorDrawer {
	public:
		AssetType GetAssetType() const override { return AssetType::PaintMaterial; }
		void Draw(const EditorPanelContext& context, const AssetMeta& meta) override;
		bool HasPendingChanges() const override { return loaded_ && saved_ != draft_; }
		bool ApplyPendingChanges(const EditorPanelContext& context) override;
		void DiscardPendingChanges() override { draft_ = saved_; }
		AssetID GetEditingAsset() const override { return selected_; }

	private:
		InspectorMaterialPreview preview_;
		AssetID selected_{};
		PaintMaterialAsset saved_{};
		PaintMaterialAsset draft_{};
		std::string status_;
		bool loaded_ = false;
	};
}
