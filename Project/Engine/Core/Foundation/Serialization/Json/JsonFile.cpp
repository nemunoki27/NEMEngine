#include "JsonFile.h"

//============================================================================
//	include
//============================================================================
#include "JsonCanonical.h"
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <fstream>
#include <iterator>

using namespace Engine;

void JsonFile::Save(const std::string& directoryFilePath, const nlohmann::json& data) {

	Save(Algorithm::PathFromUTF8(directoryFilePath), data);
}

void JsonFile::Save(const std::filesystem::path& directoryFilePath, const nlohmann::json& data) {

	// 親ディレクトリが無ければ作成する、ゲーム側Configなど初回保存でも失敗しないようにする
	const std::filesystem::path parentPath = directoryFilePath.parent_path();
	if (!parentPath.empty()) {
		std::error_code ec;
		std::filesystem::create_directories(parentPath, ec);
	}

	std::ofstream file(directoryFilePath);

	// 書き込めなかった場合
	if (!file.is_open()) {

		Assert::Call(false, "JSONファイルの保存に失敗しました: " + Algorithm::PathToUTF8(directoryFilePath));
		return;
	}

	file << data.dump(4); // インデント4で保存
}

bool JsonFile::SaveCanonical(const std::filesystem::path& directoryFilePath,
	const nlohmann::json& data, int32_t indent) {

	const std::string serialized = JsonCanonical::SerializeCanonical(data, indent);
	if (serialized.empty()) {
		return false;
	}

	std::ifstream currentFile(directoryFilePath, std::ios::binary);
	const std::string current((std::istreambuf_iterator<char>(currentFile)),
		std::istreambuf_iterator<char>());
	currentFile.close();
	if (current == serialized) {
		return true;
	}

	const std::filesystem::path parentPath = directoryFilePath.parent_path();
	std::error_code ec;
	if (!parentPath.empty()) {
		std::filesystem::create_directories(parentPath, ec);
		if (ec) {
			return false;
		}
	}

	std::filesystem::path tempPath = directoryFilePath;
	tempPath += L".tmp";
	{
		std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
		if (!file.is_open()) {
			return false;
		}
		file.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
		file.flush();
		if (!file.good()) {
			file.close();
			std::filesystem::remove(tempPath, ec);
			return false;
		}
	}

	const bool targetExists = std::filesystem::exists(directoryFilePath, ec);
	std::filesystem::path backupPath = directoryFilePath;
	backupPath += L".bak";
	if (targetExists) {

		std::filesystem::remove(backupPath, ec);
		ec.clear();
		std::filesystem::rename(directoryFilePath, backupPath, ec);
		if (ec) {
			std::filesystem::remove(tempPath, ec);
			return false;
		}
	}

	ec.clear();
	std::filesystem::rename(tempPath, directoryFilePath, ec);
	if (ec) {

		std::error_code rollbackError;
		if (targetExists) {
			std::filesystem::rename(backupPath, directoryFilePath, rollbackError);
		}
		std::filesystem::remove(tempPath, rollbackError);
		return false;
	}
	if (targetExists) {
		std::filesystem::remove(backupPath, ec);
	}
	return true;
}

nlohmann::json JsonFile::Load(const std::string& directoryFilePath, bool assertion) {

	return Load(Algorithm::PathFromUTF8(directoryFilePath), assertion);
}

nlohmann::json JsonFile::Load(const std::filesystem::path& directoryFilePath, bool assertion) {

	std::ifstream file(directoryFilePath);

	// 読み込めなかった場合
	if (!file.is_open()) {
		if (assertion) {

			Assert::Call(false, "JSONファイルの読み込みに失敗しました: " + Algorithm::PathToUTF8(directoryFilePath));
		}
		return nlohmann::json();
	}

	nlohmann::json data;
	try {
		file >> data;
	}
	catch (const nlohmann::json::parse_error& e) {
		if (assertion) {

			Assert::Call(false, "JSONファイルの解析に失敗しました: " +
				Algorithm::PathToUTF8(directoryFilePath) + "\n" + e.what());
		}
		return nlohmann::json();
	}

	return data;
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
