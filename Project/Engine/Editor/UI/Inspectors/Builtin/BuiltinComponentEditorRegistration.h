#pragma once

namespace Engine {

	// front
	class ComponentEditorRegistry;
	class MeshRendererInspectorDrawer;

	//============================================================================
	//	BuiltinComponentEditorRegistration
	//	ビルトインコンポーネントの編集情報をまとめてComponentEditorRegistryへ登録する
	//============================================================================

	// meshRendererDrawerはモデルプレビュー描画で使う参照を呼び出し側へ返す
	void RegisterBuiltinComponentEditors(ComponentEditorRegistry& registry,
		MeshRendererInspectorDrawer*& meshRendererDrawer);

} // Engine
