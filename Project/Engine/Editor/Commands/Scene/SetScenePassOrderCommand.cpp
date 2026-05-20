#include "SetScenePassOrderCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	SetScenePassOrderCommand classMethods
//============================================================================

Engine::SetScenePassOrderCommand::SetScenePassOrderCommand(
	std::vector<ScenePassDesc> beforePassOrder, std::vector<ScenePassDesc> afterPassOrder) :
	beforePassOrder_(std::move(beforePassOrder)),
	afterPassOrder_(std::move(afterPassOrder)) {
}

bool Engine::SetScenePassOrderCommand::Execute(EditorCommandContext& context) {

	// Redo時も同じ処理で変更後passOrderを適用する
	return Apply(context, afterPassOrder_);
}

void Engine::SetScenePassOrderCommand::Undo(EditorCommandContext& context) {

	// Undo時はコンストラクタで受け取った変更前passOrderへ戻す
	Apply(context, beforePassOrder_);
}

bool Engine::SetScenePassOrderCommand::Apply(
	EditorCommandContext& context, const std::vector<ScenePassDesc>& passOrder) const {

	if (!context.CanEditScene() || !context.editorContext || !context.editorContext->activeSceneHeader) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"SetScenePassOrderCommand: active scene header is not available.");
		return false;
	}

	// EditorContextは閲覧用にconstを渡しているが、実体はEditWorld側のSceneInstance::header。
	// passOrder編集はEditorCommand経由に限定して、Undo/Redoと未保存フラグへ乗せる。
	SceneHeader* header = const_cast<SceneHeader*>(context.editorContext->activeSceneHeader);
	header->passOrder = passOrder;
	return true;
}
