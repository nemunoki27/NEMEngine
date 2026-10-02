#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Volumes/VolumeProfileAsset.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	VolumeProfileTool class
	//============================================================================
	class VolumeProfileTool final :
		public IEditorTool {
	public:
		VolumeProfileTool() = default;
		~VolumeProfileTool() override = default;

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;
		void OpenAsset(AssetID assetID);
		bool HasPendingEdits() const override { return dirty_; }
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		ToolDescriptor descriptor_{
			.id = "engine.volume_profile",
			.name = "Volume Profile",
			.category = "レンダリング",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 1,
		};

		AssetID assetID_{};
		AssetID requestedAsset_{};
		AssetID pendingAsset_{};
		VolumeProfileAsset draft_{};
		VolumeProfileAsset runtimeDraft_{};
		bool openWindow_ = false;
		bool dirty_ = false;
		bool pendingClose_ = false;
		bool wasPlaying_ = false;
		std::string statusMessage_{};

		bool Load(const EditorToolContext& context, AssetID assetID);
		bool Save(const EditorToolContext& context);
		void DiscardPreview(const EditorToolContext& context);
		void DrawUnsavedChangesPopup(const EditorToolContext& context);
		void DrawSettings(const EditorToolContext& context,
			VolumeProfileAsset& profile, bool persistent);
	};
} // Engine
