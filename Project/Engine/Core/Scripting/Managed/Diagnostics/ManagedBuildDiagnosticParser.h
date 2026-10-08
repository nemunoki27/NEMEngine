#pragma once

//============================================================================
//	include
//============================================================================
#include "ManagedBuildDiagnosticTypes.h"

// c++
#include <optional>
#include <string>

namespace Engine::ManagedBuildDiagnosticParser {

	// 出力行から位置と重大度を読み診断以外は未取得を返す
	std::optional<ManagedBuildDiagnostic> ParseLine(const std::string& rawLine);
}
