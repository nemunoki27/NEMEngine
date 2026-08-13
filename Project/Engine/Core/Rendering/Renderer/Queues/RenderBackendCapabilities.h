#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>

// c++
#include <cstdint>

namespace Engine::RenderBackendCapabilities {

	//============================================================================
	//	バックエンドの対応機能、backendID単位の判定を1箇所へ集約する
	//	対応バックエンドを増やす時はここだけ直せば各パスの判定に反映される
	//============================================================================

	// 選択/実行時アウトラインのマスク描画に対応しているか
	inline bool SupportsOutlineMask(uint32_t backendID) {

		return backendID == RenderBackendID::Mesh ||
			backendID == RenderBackendID::Primitive;
	}

} // namespace Engine::RenderBackendCapabilities
