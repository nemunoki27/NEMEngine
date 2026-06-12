#pragma once

//============================================================================
//	include
//============================================================================
#include <cstdint>

namespace Engine {

	//============================================================================
	//	AlcUnloadStatus enum
	//	collectible ALCのunload結果を表すtyped status
	//============================================================================
	enum class AlcUnloadStatus : int32_t {

		Unknown = 0,
		UnloadSucceeded = 1,
		LeakSuspected = 2,
	};
} // Engine
