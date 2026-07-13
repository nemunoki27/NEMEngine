#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <memory>
#include <string>
#include <vector>

namespace Engine {

	// 前方宣言
	class IEditorPanel;
	class TextureUploadService;

	//============================================================================
	//	EditorPanelCreateContext structure
	//	ビルトインパネル生成に必要な共通依存をまとめる
	//============================================================================
	struct EditorPanelCreateContext {

		// サムネイルなどを持つパネルが使うテクスチャアップロードサービス
		TextureUploadService& textureUploadService;
	};

	// ビルトインエディターパネルを表示順どおりに生成して返す
	std::vector<std::unique_ptr<IEditorPanel>> CreateBuiltinEditorPanels(const EditorPanelCreateContext& context);
	// 複製可能なビルトインパネルを生成して返す
	std::unique_ptr<IEditorPanel> CreateBuiltinEditorPanelInstance(const EditorPanelCreateContext& context,
		const std::string& typeID, const std::string& instanceID, const std::string& displayName = {});

} // Engine
