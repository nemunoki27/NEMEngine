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

		bool Tick(ToolContext& context);
		void RequestProfile(AssetID assetID) { requestedProfile_ = assetID; }
		void SelectProfile(const EditorToolContext& context, AssetID profileAsset);
		bool CanCreateProfile(const EditorToolContext& context) const;
		bool CreateProfile(const EditorToolContext& context);
		bool ImportProfileSettings(const EditorToolContext& context, AssetID sourceProfile);
		void Save();
		void Reload();
		void SetDirty();
		void SetStatusMessage(const std::string& message, bool error);
		static bool IsPassMaterialSource(AssetType assetType, std::string_view assetPath);
		AssetID ResolvePassMaterial(const EditorToolContext& context, AssetID assetID,
			AssetType assetType, std::string_view assetPath);

		//--------- accessor -----------------------------------------------------

		AssetID GetProfileID() const { return observedProfile_; }
		const std::string& GetStatusMessage() const { return statusMessage_; }
		bool HasError() const { return statusError_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		AssetID requestedProfile_{};
		AssetID observedProfile_{};
		std::string statusMessage_{};
		bool statusError_ = false;
	};
}
