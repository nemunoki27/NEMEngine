#include "FBXDocumentReferences.h"

//============================================================================
//	include
//============================================================================
#include "FBXDocumentParser.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <stdexcept>
#include <utility>

bool Engine::FBXDocumentReferences::IsDocumentPath(const std::filesystem::path& path) {

	return Algorithm::ToLower(Algorithm::PathToUTF8(path.extension())) == ".fbx";
}

bool Engine::FBXDocumentReferences::Rewrite(
	std::string& bytes, const GLTFDocumentReferences::ReferenceRewrite& rewrite, std::string& diagnostic) {

	diagnostic.clear();
	try {
		// 途中失敗では元の文書を維持する
		auto result = bytes.starts_with("Kaydara FBX Binary")
			? FBXDocumentParser::RewriteBinary(bytes, rewrite) : FBXDocumentParser::RewriteASCII(bytes, rewrite);
		bytes = std::move(result);
		return true;
	} catch (const std::exception& error) {
		diagnostic = "FBXの画像参照を更新できません: " + std::string(error.what());
		return false;
	}
}

bool Engine::FBXDocumentParser::IsReference(std::string_view name, std::string_view parent) {

	return (parent == "Texture" || parent == "Video") &&
		(name == "FileName" || name == "Filename" || name == "RelativeFilename");
}

void Engine::FBXDocumentParser::RewriteReference(
	std::string& reference, const GLTFDocumentReferences::ReferenceRewrite& rewrite) {

	// 空の補助参照と埋め込み参照は外部画像として扱わない
	if (reference.empty() || reference.starts_with('*')) {
		return;
	}
	if (!rewrite(reference)) {
		throw std::runtime_error("外部画像を解決できません");
	}
	if (reference.empty() || reference.find_first_of("\r\n\0\"", 0, 4) != std::string::npos) {
		throw std::runtime_error("画像参照に不正な文字があります");
	}
}
