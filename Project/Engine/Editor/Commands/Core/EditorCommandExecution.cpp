#include "EditorCommandExecution.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorSceneEditScope.h>
#include <Engine/Editor/Commands/Core/EditorSelectionSnapshot.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <exception>

bool Engine::EditorCommandExecution::Run(const EditorContext& context, EditorState& state,
	EditorSceneDirtyState& dirtyState, const std::function<bool(EditorCommandContext&)>& operation) {

	EditorCommandContext commandContext;
	commandContext.editorContext = &context;
	commandContext.editorState = &state;
	commandContext.allowRuntimeEdit = context.isPlaying;
	EditorSceneEditScope editScope(context, dirtyState);
	EditorSelectionSnapshot selection;
	selection.Capture(commandContext);
	try {
		const bool executed = operation(commandContext);
		if (executed) {
			editScope.Commit();
		}
		return executed;
	} catch (const std::exception& error) {
		Logger::Output(LogType::Engine, spdlog::level::err, "編集操作に失敗しました: {}", error.what());
	} catch (...) {
		Logger::Output(LogType::Engine, spdlog::level::err, "編集操作で不明な例外が発生しました");
	}

	// 取消まで失敗した場合も変更したSceneの保存対象を残す
	selection.Restore(commandContext);
	editScope.Commit();
	return false;
}
