#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/EditorCommandContext.h>
#include <Engine/Core/Foundation/Utility/Command/ICommand.h>
#include <Engine/Core/Foundation/Utility/Command/CommandHistory.h>

namespace Engine {

	// エディタコマンドで使用する型エイリアス
	using IEditorCommand = ICommand<EditorCommandContext>;
	using EditorCommandHistory = CommandHistory<EditorCommandContext>;
} // Engine