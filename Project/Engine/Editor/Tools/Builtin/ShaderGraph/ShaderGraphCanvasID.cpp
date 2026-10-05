#include "ShaderGraphCanvasID.h"

//============================================================================
//	include
//============================================================================

namespace Engine::ShaderGraphCanvasID {

	// 保存IDをNode EditorのIDへ変換する
	uintptr_t ToNodeEditorID(Engine::UUID id) {

		return static_cast<uintptr_t>(id.value);
	}

	// Nodeと入出力番号からPinを識別する
	uintptr_t MakePinID(Engine::UUID node, bool input, uint32_t slot) {

		uint64_t value = node.value;
		value ^= input ? 0x9e3779b97f4a7c15ull : 0xc2b2ae3d27d4eb4full;
		value ^= static_cast<uint64_t>(slot + 1u) * 0x165667b19e3779f9ull;
		return static_cast<uintptr_t>(value != 0 ? value : 1);
	}
} // Engine::ShaderGraphCanvasID
