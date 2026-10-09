#pragma once

//============================================================================
//	include
//============================================================================
#include "GLTFDocumentReferences.h"

namespace Engine::FBXDocumentReferences {

	// FBXの文書拡張子を判定する
	bool IsDocumentPath(const std::filesystem::path& path);

	// ASCIIとバイナリの外部画像参照を更新する
	bool Rewrite(std::string& bytes, const GLTFDocumentReferences::ReferenceRewrite& rewrite, std::string& diagnostic);
}
