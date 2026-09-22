#include "ProjectSettingsOperations.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Commands/Entity/RemapEntityTagsCommand.h>

// c++
#include <memory>

void Engine::ProjectSettingsOperations::RemapTags(const EditorToolContext& context, const std::string& from, const std::string& to) {

	// 開いているシーンのタグ文字列だけ付け替える、コマンド経由でUndoできる
	if (!context.CanEditScene() || !context.panelContext || !context.panelContext->host) {
		return;
	}
	context.panelContext->host->ExecuteEditorCommand(std::make_unique<RemapEntityTagsCommand>(from, to));
}
