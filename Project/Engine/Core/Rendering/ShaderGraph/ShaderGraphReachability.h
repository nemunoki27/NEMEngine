#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAsset.h"

// c++
#include <span>

namespace Engine::ShaderGraphReachability {

	// 指定ノードの入力へつながる部分だけを残す
	void Retain(ShaderGraphAsset& graph, UUID root, std::span<const uint32_t> inputSlots = {},
		bool retainParameters = false);
} // Engine::ShaderGraphReachability
