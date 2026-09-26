#include "JsonFile.h"

//============================================================================
//	include
//============================================================================
#include "JsonCanonical.h"
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <fstream>

using namespace Engine;

bool JsonFile::Save(const std::string& directoryFilePath, const nlohmann::json& data) {

	return Save(Algorithm::PathFromUTF8(directoryFilePath), data);
}

bool JsonFile::Save(const std::filesystem::path& directoryFilePath, const nlohmann::json& data) {

	// 非有限値をnullへ置き換えず保存失敗として返す
	if (JsonCanonical::SerializeCanonical(data, 4).empty()) {
		return false;
	}
	return StorageFileUtility::WriteBytes(directoryFilePath, data.dump(4));
}

bool JsonFile::SaveCanonical(const std::filesystem::path& directoryFilePath,
	const nlohmann::json& data, int32_t indent) {

	const std::string serialized = JsonCanonical::SerializeCanonical(data, indent);
	return !serialized.empty() && StorageFileUtility::WriteBytes(directoryFilePath, serialized);
}

nlohmann::json JsonFile::Load(const std::string& directoryFilePath, bool assertion) {

	return Load(Algorithm::PathFromUTF8(directoryFilePath), assertion);
}

nlohmann::json JsonFile::Load(const std::filesystem::path& directoryFilePath, bool assertion) {

	nlohmann::json output;
	std::string diagnostic;
	if (!TryLoad(directoryFilePath, output, &diagnostic) && assertion) {
		Assert::Call(false, diagnostic);
	}
	return output;
}

bool JsonFile::TryLoad(const std::filesystem::path& path, nlohmann::json& output, std::string* diagnostic) {

	if (diagnostic) {
		diagnostic->clear();
	}
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) {
		if (diagnostic) {
			*diagnostic = "JSONファイルの読み込みに失敗しました: " + Algorithm::PathToUTF8(path);
		}
		return false;
	}
	try {
		// 解析完了まで呼出し元のデータを変更しない
		nlohmann::json parsed = nlohmann::json::parse(file);
		if (file.bad()) {
			if (diagnostic) {
				*diagnostic = "JSONファイルの読み込みが中断されました: " + Algorithm::PathToUTF8(path);
			}
			return false;
		}
		output = std::move(parsed);
		return true;
	} catch (const nlohmann::json::exception& error) {
		if (diagnostic) {
			*diagnostic = "JSONファイルの解析に失敗しました: " + Algorithm::PathToUTF8(path) + "\n" + error.what();
		}
		return false;
	}
}

bool JsonFile::Check(const std::string& directoryFilePath, bool assertion) {

	return Check(Algorithm::PathFromUTF8(directoryFilePath), assertion);
}

bool JsonFile::Check(const std::filesystem::path& directoryFilePath, bool assertion) {

	std::ifstream file(directoryFilePath);

	// 読み込めなかった場合
	if (!file.is_open()) {
		if (assertion) {

			Assert::Call(false, "JSONファイルの読み込みに失敗しました: " + Algorithm::PathToUTF8(directoryFilePath));
		}
		return false;
	}
	return true;
}
