#include "EditorToolContext.h"

Engine::EditorToolContext Engine::MakeEditorToolContext(const EditorPanelContext& context) {

	EditorToolContext result;
	result.panelContext = &context;
	if (!context.editorContext) return result;
	const auto& editor = *context.editorContext;
	result.toolContext.world = editor.activeWorld;
	result.toolContext.assetDatabase = editor.assetDatabase;
	result.toolContext.systemContext = editor.systemContext;
	result.toolContext.sceneInstances = editor.sceneInstances;
	result.toolContext.activeSceneHeader = editor.activeSceneHeader;
	result.toolContext.activeSceneAsset = editor.activeSceneAsset;
	result.toolContext.activeSceneInstanceID = editor.activeSceneInstanceID;
	result.toolContext.activeScenePath = editor.activeScenePath;
	result.toolContext.isPlaying = editor.isPlaying;
	result.toolContext.canEditScene = context.CanEditScene();
	return result;
}
