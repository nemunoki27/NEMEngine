#pragma once

//============================================================================
//	include
//============================================================================
//============================================================================
//	BuildConfig namespace
//============================================================================
namespace Engine::BuildConfig {

#if defined(NEM_EDITOR_UI_ENABLED) || defined(_DEBUG) || defined(_DEVELOPBUILD)

	inline constexpr bool kEditorEnabled = true;
#else

	inline constexpr bool kEditorEnabled = false;
#endif
}
