#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphPreviewFingerprint.h"

namespace Engine::ShaderGraphNodePreviewUtility {

	inline std::string_view OutputPinLabel(const ShaderGraphNode& node, uint32_t slot) {

		const auto name = GetShaderGraphOutputName(node, slot);
		return node.outputPorts.empty() && name == "Output" ? "出力" : name;
	}

	// プレビューで扱えるノードか確認する
	bool IsPreviewableNode(Engine::ShaderGraphNodeKind kind);
	// 出力を持つノードか確認する
	bool IsPreviewableNode(const Engine::ShaderGraphNode& node);
} // Engine::ShaderGraphNodePreviewUtility
