#include "ModelDocumentReferences.h"

//============================================================================
//	include
//============================================================================
#include "GLTFFileReference.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <stdexcept>
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace {

	// OBJの空白だけを判定する
	bool IsHorizontalSpace(char value) {

		return value == ' ' || value == '\t';
	}

	// OBJとMTLの行から参照開始位置を取り出す
	size_t FindReference(std::string_view line, bool material) {

		size_t start = line.starts_with("\xEF\xBB\xBF") ? 3 : 0;
		const auto next = [&]() {
			while (start < line.size() && !IsHorizontalSpace(line[start])) {
				++start;
			}
			while (start < line.size() && IsHorizontalSpace(line[start])) {
				++start;
			}
		};
		while (start < line.size() && IsHorizontalSpace(line[start])) {
			++start;
		}
		const auto tokenEnd = line.find_first_of(" \t", start);
		const auto token = line.substr(start, tokenEnd == std::string_view::npos ? line.size() - start : tokenEnd - start);
		if (!material) {
			if (token != "mtllib") {
				return std::string_view::npos;
			}
			next();
			return start;
		}
		constexpr std::array<std::string_view, 16> textures{"map_kd", "map_ka", "map_ks", "map_d", "map_emissive",
			"map_ke", "map_bump", "bump", "map_kn", "norm", "map_disp", "disp", "map_ns", "map_pr", "map_pm", "map_ps"};
		const auto keyword = Engine::Algorithm::ToLower(std::string(token));
		if (std::ranges::find(textures, keyword) == textures.end()) {
			return std::string_view::npos;
		}
		next();
		// Assimpと同じ数のTextureオプションを読み飛ばす
		while (start < line.size() && line[start] == '-') {
			const auto end = line.find_first_of(" \t", start);
			const auto option = Engine::Algorithm::ToLower(std::string(line.substr(start, end - start)));
			size_t count = 1;
			if (option == "-o" || option == "-s" || option == "-t") {
				count = 4;
			} else if (option == "-mm") {
				count = 3;
			} else if (option == "-clamp" || option == "-type" || option == "-bm" || option == "-blendu" ||
				option == "-blendv" || option == "-boost" || option == "-texres" || option == "-imfchan") {
				count = 2;
			}
			for (size_t index = 0; index < count; ++index) {
				next();
			}
		}
		return start;
	}

	// 行末と参照以外の文字を保持して文書を更新する
	bool RewriteLines(std::string& bytes, bool material, const Engine::GLTFDocumentReferences::ReferenceRewrite& rewrite) {

		std::string result;
		result.reserve(bytes.size());
		bool changed = false;
		for (size_t start = 0; start < bytes.size();) {
			const size_t lineEnd = bytes.find_first_of("\r\n", start);
			const size_t end = lineEnd == std::string::npos ? bytes.size() : lineEnd;
			const auto referenceStart = FindReference(std::string_view(bytes).substr(start, end - start), material);
			if (referenceStart != std::string_view::npos && referenceStart < end - start) {
				const size_t value = start + referenceStart;
				// Assimpと同じく行全体を参照名として扱う
				std::string reference = bytes.substr(value, end - value);
				if (!reference.empty() && !rewrite(reference)) {
					throw std::runtime_error("モデル文書の参照を更新できません");
				}
				if (reference.find_first_of("\r\n\0", 0, 3) != std::string::npos) {
					throw std::runtime_error("モデル文書の参照に不正な文字があります");
				}
				changed = changed || reference != std::string_view(bytes).substr(value, end - value);
				result.append(bytes, start, value - start);
				result += reference;
			} else {
				result.append(bytes, start, end - start);
			}
			start = end;
			// CRLFと最終行の終端をそのまま保持する
			while (start < bytes.size() && (bytes[start] == '\r' || bytes[start] == '\n')) {
				result += bytes[start++];
			}
		}
		if (changed) {
			bytes = std::move(result);
		}
		return true;
	}
}

bool Engine::ModelDocumentReferences::IsDocumentPath(const std::filesystem::path& path) {

	return GLTFDocumentReferences::IsDocumentPath(path) ||
		   Algorithm::ToLower(Algorithm::PathToUTF8(path.extension())) == ".obj";
}

bool Engine::ModelDocumentReferences::Rewrite(const std::filesystem::path& path, std::string& bytes,
	const GLTFDocumentReferences::ReferenceRewrite& rewrite, std::string& diagnostic) {

	if (GLTFDocumentReferences::IsDocumentPath(path)) {
		return GLTFDocumentReferences::Rewrite(bytes, rewrite, diagnostic);
	}
	diagnostic.clear();
	try {
		if (!IsDocumentPath(path)) {
			throw std::runtime_error("モデル文書の形式に対応していません");
		}
		return RewriteLines(bytes, false, rewrite);
	} catch (const std::exception& error) {
		diagnostic = error.what();
		return false;
	}
}

bool Engine::ModelDocumentReferences::RewriteMaterial(std::string& bytes,
	const GLTFDocumentReferences::ReferenceRewrite& rewrite, std::string& diagnostic) {

	diagnostic.clear();
	try {
		return RewriteLines(bytes, true, rewrite);
	} catch (const std::exception& error) {
		diagnostic = error.what();
		return false;
	}
}

bool Engine::ModelDocumentReferences::Rebase(
	const std::filesystem::path& source, const std::filesystem::path& target, std::string& bytes, std::string& diagnostic) {

	diagnostic.clear();
	try {
		const auto sourceDirectory = std::filesystem::absolute(source).parent_path().lexically_normal();
		const auto targetDirectory = std::filesystem::absolute(target).parent_path().lexically_normal();
		const bool obj = !GLTFDocumentReferences::IsDocumentPath(source);
		return Rewrite(
			source, bytes,
			[&](std::string& uri) {
				auto reference = obj ? Algorithm::PathFromUTF8(uri) : GLTFFileReference::Decode(uri);
				if (!obj && reference.is_absolute()) {
					return true;
				}
				auto resolved = (sourceDirectory / reference).lexically_normal();
				if (obj && !std::filesystem::is_regular_file(Algorithm::ToFileSystemPath(resolved))) {
					// 同名MTLによる補完先を改名後も保持する
					auto fallback = std::filesystem::absolute(source);
					fallback.replace_extension(".mtl");
					if (std::filesystem::is_regular_file(Algorithm::ToFileSystemPath(fallback))) {
						if (reference.has_parent_path()) {
							throw std::runtime_error("OBJの補完Materialと画像の参照を確定できません");
						}
						resolved = fallback.lexically_normal();
					} else {
						throw std::runtime_error("OBJのMaterial参照が見つかりません");
					}
				}
				// 同じ配置の有効な参照はbyte列も維持する
				if (sourceDirectory == targetDirectory && resolved == (sourceDirectory / reference).lexically_normal()) {
					return true;
				}
				const auto relative = resolved.lexically_relative(targetDirectory);
				if (obj && relative.empty()) {
					throw std::runtime_error("OBJのMaterial参照を別ドライブへ移動できません");
				}
				// 文書内のパスは共通の区切りで保存する
				const auto path = relative.empty() ? resolved : relative;
				uri = obj ? Algorithm::ConvertString(path.generic_wstring()) : GLTFFileReference::Encode(path);
				return true;
			},
			diagnostic);
	} catch (const std::exception& error) {
		diagnostic = error.what();
		return false;
	}
}
