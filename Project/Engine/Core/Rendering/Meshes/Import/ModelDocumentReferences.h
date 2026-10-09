#pragma once

//============================================================================
//	include
//============================================================================
#include "GLTFDocumentReferences.h"

namespace Engine::ModelDocumentReferences {

	// 共有ファイルを参照するモデル文書を判定する
	bool IsDocumentPath(const std::filesystem::path& path);

	// OBJのMaterial参照とglTFとFBXの外部参照だけを書き換える
	bool Rewrite(const std::filesystem::path& path, std::string& bytes, const GLTFDocumentReferences::ReferenceRewrite& rewrite,
		std::string& diagnostic);
	// MTLの画像参照を更新し、描画値とTextureオプションを保持する
	bool RewriteMaterial(std::string& bytes, const GLTFDocumentReferences::ReferenceRewrite& rewrite,
		std::string& diagnostic);

	// 配置を変えても元の共有ファイルを参照する
	bool Rebase(
		const std::filesystem::path& source, const std::filesystem::path& target, std::string& bytes, std::string& diagnostic);
}
