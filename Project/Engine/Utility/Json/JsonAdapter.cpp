#include "JsonAdapter.h"

using namespace Engine;

//============================================================================*/
//	include
//============================================================================*/
#include <Engine/Debug/Assert.h>

//============================================================================*/
//	JsonAdapter classMethods
//============================================================================*/

void JsonAdapter::Save(const std::string& directoryFilePath, const nlohmann::json& data) {

	// フォルダのフルパス取得
	std::string fullPath = directoryFilePath;
	std::filesystem::path path(fullPath);

	if (path.extension() != ".json") {

		path.replace_extension(".json");
		fullPath = path.string();
	}

	// フォルダが存在しないなら作成する
	if (!std::filesystem::exists(path.parent_path())) {
		std::filesystem::create_directories(path.parent_path());
	}

	std::ofstream file(fullPath);
	file << data.dump(4);
	file.close();
}

nlohmann::json Engine::JsonAdapter::Load(const std::string& directoryFilePath, bool assertion) {

	nlohmann::json out = nlohmann::json::object();

	std::string fullPath = directoryFilePath;
	std::ifstream file(fullPath);

	// 読み込めなかった場合
	if (!file.is_open()) {
		if (assertion) {

			Assert::Call(false, "Failed to load nlohmann::json file: " + fullPath);
		}
		return nlohmann::json();
	}

	file >> out;
	file.close();

	return out;
}

bool Engine::JsonAdapter::Check(const std::string& directoryFilePath, bool assertion) {

	std::string fullPath = directoryFilePath;
	std::ifstream file(fullPath);

	// 読み込めなかった場合
	if (!file.is_open()) {
		if (assertion) {

			Assert::Call(false, "Failed to load nlohmann::json file: " + fullPath);
		}
		return false;
	}
	return true;
}