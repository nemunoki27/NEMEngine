#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/Core/ToolTypes.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

namespace Engine {

	//============================================================================
	//	EditorToolContext struct
	//============================================================================

	// ImGuiを使うエディタ専用ツールに渡すコンテキスト
	struct EditorToolContext {

		const EditorPanelContext* panelContext = nullptr;
		ToolContext toolContext{};

		ECSWorld* GetWorld() const { return toolContext.world; }
		bool CanEditScene() const { return toolContext.canEditScene; }
		bool IsPlaying() const { return toolContext.isPlaying; }
	};
} // Engine
