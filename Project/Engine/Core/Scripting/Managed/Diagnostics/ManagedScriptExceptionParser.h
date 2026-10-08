#pragma once

//============================================================================
//	include
//============================================================================
#include "ManagedScriptExceptionStore.h"

namespace Engine {

	// 診断JSONから表示用の例外情報を読み込む
	bool ParseManagedScriptException(const char* jsonUTF8, ManagedScriptException& entry);
}
