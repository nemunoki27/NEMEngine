#include "SetSceneRenderPathCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <utility>

//============================================================================
//	SetSceneRenderPathCommand classMethods
//============================================================================

Engine::SetSceneRenderPathCommand::SetSceneRenderPathCommand(
	std::vector<SceneRenderTargetDesc> beforeRenderTargets,
	std::vector<ScenePassDesc> beforePassOrder,
	std::vector<SceneRenderTargetDesc> afterRenderTargets,
	std::vector<ScenePassDesc> afterPassOrder) :
	beforeRenderTargets_(std::move(beforeRenderTargets)),
	beforePassOrder_(std::move(beforePassOrder)),
	afterRenderTargets_(std::move(afterRenderTargets)),
	afterPassOrder_(std::move(afterPassOrder)) {
}

bool Engine::SetSceneRenderPathCommand::Execute(EditorCommandContext& context) {

	// Redo時も同じ処理で変更後の描画情報を適用する
	return Apply(context, afterRenderTargets_, afterPassOrder_);
}

void Engine::SetSceneRenderPathCommand::Undo(EditorCommandContext& context) {

	// Undo時はコンストラクタで受け取った変更前の描画情報へ戻す
	Apply(context, beforeRenderTargets_, beforePassOrder_);
}

bool Engine::SetSceneRenderPathCommand::Apply(EditorCommandContext& context,
	const std::vector<SceneRenderTargetDesc>& renderTargets,
	const std::vector<ScenePassDesc>& passOrder) const {

	if (!context.CanEditScene() || !context.editorContext || !context.editorContext->activeSceneHeader) {
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"SetSceneRenderPathCommand: active scene header is not available.");
		return false;
	}

	// EditorCommand経由で差し替え、Undo/Redoと未保存フラグへ乗せる。
	SceneHeader* header = const_cast<SceneHeader*>(context.editorContext->activeSceneHeader);
	header->renderTargets = renderTargets;
	header->passOrder = passOrder;
	return true;
}
