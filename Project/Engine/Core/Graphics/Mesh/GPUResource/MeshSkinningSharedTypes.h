#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	MeshSkinningSharedTypes structures
	//============================================================================

	// GPUスキニング用の共通定数
	struct MeshSkinningDispatchConstants {

		uint32_t vertexCount = 0;
		uint32_t boneCount = 0;
		uint32_t skinnedInstanceCount = 0;
		uint32_t _pad0 = 0;
	};
} // Engine