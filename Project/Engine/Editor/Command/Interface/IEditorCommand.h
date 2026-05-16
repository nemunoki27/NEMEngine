#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/EditorCommandContext.h>
#include <Engine/Core/Utility/Command/ICommand.h>
#include <Engine/Core/Utility/Command/CommandHistory.h>

namespace Engine {

	// エディタコマンドで使用する型エイリアス
	using IEditorCommand = ICommand<EditorCommandContext>;
	using EditorCommandHistory = CommandHistory<EditorCommandContext>;
} // Engine