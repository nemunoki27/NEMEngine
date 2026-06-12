#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

// c++
#include <string>

namespace Engine {

	class TextureUploadService;

	//============================================================================
	//	ToolbarPanel class
	//	ツールバーパネル
	//============================================================================
	class ToolbarPanel :
		public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit ToolbarPanel(TextureUploadService& textureUploadService);
		~ToolbarPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct IconSet {

			std::string playKey;
			std::string stopKey;
			std::string pauseKey;
			std::string frameStepKey;
		};

		//--------- variables ----------------------------------------------------

		TextureUploadService* textureUploadService_ = nullptr;
		IconSet icons_{};
		bool iconsRequested_ = false;

		//--------- functions ----------------------------------------------------

		void RequestIcons();
		ImTextureID GetTextureID(const std::string& key) const;
		bool DrawIconButton(const char* id, ImTextureID textureID, const ImVec2& size) const;
	};
} // Engine
