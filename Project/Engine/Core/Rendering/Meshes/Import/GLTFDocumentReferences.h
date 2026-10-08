#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <functional>
#include <filesystem>
#include <string>

namespace Engine::GLTFDocumentReferences {

	// glTFとGLBの文書拡張子を判定する
	bool IsDocumentPath(const std::filesystem::path& path);

	// 外部参照の文字列を更新し、解決できない場合は失敗を返す
	using ReferenceRewrite = std::function<bool(std::string&)>;

	// glTFとGLBのBufferと画像参照だけを書き換える
	bool Rewrite(std::string& bytes, const ReferenceRewrite& rewrite, std::string& diagnostic);

}
