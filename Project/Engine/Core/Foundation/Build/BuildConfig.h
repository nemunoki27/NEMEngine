#pragma once

//============================================================================
//	include
//============================================================================

//============================================================================
//	BuildConfig namespace
//============================================================================
namespace Engine::BuildConfig {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	inline constexpr bool kEditorEnabled = true;
#else

	inline constexpr bool kEditorEnabled = false;
#endif
}