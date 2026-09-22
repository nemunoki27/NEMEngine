#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureProfile.h"
#include <string_view>

namespace Engine::RenderFeatureProfileValidation {

	// 省略時の主出力名を解決する
	std::string_view ResolveOutputName(const RenderFeatureOutputReference& reference);
	// 出力名の存在を確認する
	bool HasOutput(const RenderFeaturePassSettings& pass, std::string_view outputName);
	// ProfileのIDと入出力参照を検証する
	bool Validate(const RenderFeatureProfileAsset& profile, std::string& diagnostic);
}
