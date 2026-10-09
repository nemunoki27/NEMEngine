#include "FBXImportTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Import/FBXDocumentReferences.h>

// c++
#include <array>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

	// 検証用整数をLittle Endianで保存する
	void WriteInteger(std::string& bytes, size_t position, size_t width, uint64_t value) {

		for (size_t index = 0; index < width; ++index) {
			bytes[position + index] = static_cast<char>(value >> (index * 8));
		}
	}
	// 同じ構造を32bitと64bitのノードで保存する
	std::string BuildBinary(size_t width, const std::string& reference) {

		std::string bytes("Kaydara FBX Binary  \0\x1A\0", 23);
		bytes.resize(27);
		WriteInteger(bytes, 23, 4, width == 4 ? 7400 : 7500);
		const size_t header = width * 3 + 1;
		const auto begin = [&](std::string_view name, std::string_view properties, uint64_t count) {
			const size_t start = bytes.size();
			bytes.append(header, '\0');
			WriteInteger(bytes, start + width, width, count);
			WriteInteger(bytes, start + width * 2, width, properties.size());
			bytes[start + header - 1] = static_cast<char>(name.size());
			bytes += name;
			bytes += properties;
			return start;
		};
		const size_t objects = begin("Objects", {}, 0);
		const size_t texture = begin("Texture", {}, 0);
		std::string property(5, '\0');
		property[0] = 'S';
		WriteInteger(property, 1, 4, reference.size());
		property += reference;
		const size_t filename = begin("RelativeFilename", property, 1);
		WriteInteger(bytes, filename, width, bytes.size());
		bytes.append(header, '\0');
		WriteInteger(bytes, texture, width, bytes.size());
		bytes.append(header, '\0');
		WriteInteger(bytes, objects, width, bytes.size());
		bytes.append(header, '\0');
		bytes += "unchanged footer";
		return bytes;
	}
	// 参照長の増減と途中失敗で元文書を保持する
	bool CheckBinary(size_t width) {

		using namespace Engine;
		const auto original = BuildBinary(width, "Textures/共有 color.png");
		auto bytes = original;
		std::string diagnostic;
		for (const std::string path : {"../_resources/long directory/共有 color.png", "a.png"}) {
			size_t count = 0;
			if (!FBXDocumentReferences::Rewrite(bytes, [&](std::string& reference) {
					++count;
					reference = path;
					return true;
				}, diagnostic) || count != 1 || bytes != BuildBinary(width, path)) {
				return false;
			}
		}
		bytes = original;
		if (FBXDocumentReferences::Rewrite(bytes, [](std::string&) { return false; }, diagnostic) ||
			bytes != original || diagnostic.empty()) {
			return false;
		}
		// 不正な絶対位置と切れた終端を拒否する
		for (auto damaged : {original.substr(0, original.size() - 30), original}) {
			WriteInteger(damaged, 27, width, damaged.size() + 1);
			const auto before = damaged;
			if (FBXDocumentReferences::Rewrite(damaged, [](std::string&) { return true; }, diagnostic) ||
				damaged != before || diagnostic.empty()) {
				return false;
			}
		}
		return true;
	}
}

bool NEMTests::CheckFBXReferences() {

	using namespace Engine;
	const std::string original = "; FileName: \"ignored.png\"\r\nObjects: {\n"
		"Texture: 1, \"Texture::{name}\", \"\" { FileName: \"Textures\\color.png\"\n"
		"RelativeFilename: \"Textures\\color.png\" }\n"
		"Video: 2, \"Video::media\", \"Clip\" { Filename: \"Textures\\color.png\" }\n"
		"Model: 3, \"Model::mesh\", \"Mesh\" { FileName: \"unchanged.png\" } }\n"
		"Takes: { Take: \"Take001\" { FileName: \"unchanged.tak\" } }\n";
	std::string bytes = original;
	std::string diagnostic;
	size_t count = 0;
	if (!FBXDocumentReferences::Rewrite(bytes, [&](std::string& reference) {
			++count;
			if (reference != "Textures\\color.png") return false;
			reference = "_resources/0/color.png";
			return true;
		}, diagnostic) || count != 3 || bytes.find("FileName: \"unchanged.png\"") == std::string::npos ||
		bytes.find("FileName: \"unchanged.tak\"") == std::string::npos) {
		return false;
	}
	bytes = original;
	count = 0;
	if (FBXDocumentReferences::Rewrite(bytes, [&](std::string& reference) {
			reference = "changed.png";
			return ++count != 2;
		}, diagnostic) || bytes != original || diagnostic.empty()) {
		return false;
	}
	return CheckBinary(4) && CheckBinary(8);
}
