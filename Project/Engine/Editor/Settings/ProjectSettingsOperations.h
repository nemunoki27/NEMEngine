#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <string>

namespace Engine {

	struct EditorToolContext;

	namespace ProjectSettingsOperations {

		// 開いているWorldのタグ参照をCommandで変更する
		bool RemapTags(const EditorToolContext& context, const std::string& from, const std::string& to);
		// Project全体のRendering Layer割当を解除する
		bool ClearRenderingLayer(const EditorToolContext& context, uint32_t layerIndex);
		// Project全体のCollisionタイプ割当を削除して上位bitを詰める
		bool RemoveCollisionType(const EditorToolContext& context, uint32_t typeIndex);
	}
}
