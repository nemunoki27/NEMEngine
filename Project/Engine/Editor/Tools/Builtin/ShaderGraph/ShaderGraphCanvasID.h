#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <cstdint>

namespace Engine::ShaderGraphCanvasID {

	// 保存IDをNode EditorのIDへ変換する
	uintptr_t ToNodeEditorID(UUID id);
	// Nodeと入出力番号からPinを識別する
	uintptr_t MakePinID(UUID node, bool input, uint32_t slot);
} // Engine::ShaderGraphCanvasID
