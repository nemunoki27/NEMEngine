#pragma once

//============================================================================
//	include
//============================================================================
#include "GLTFDocumentReferences.h"

// c++
#include <string_view>

namespace Engine::FBXDocumentParser {

	// TextureとVideoの画像参照だけを対象にする
	bool IsReference(std::string_view name, std::string_view parent);
	// 不正な参照と解決失敗を呼出し元へ返す
	void RewriteReference(std::string& reference, const GLTFDocumentReferences::ReferenceRewrite& rewrite);
	// ASCIIの階層を走査して参照文字列を更新する
	std::string RewriteASCII(std::string_view bytes, const GLTFDocumentReferences::ReferenceRewrite& rewrite);
	// ノードの絶対位置を更新してバイナリ参照を書き換える
	std::string RewriteBinary(std::string_view bytes, const GLTFDocumentReferences::ReferenceRewrite& rewrite);
}
