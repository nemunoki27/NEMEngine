#include "AssetDatabase.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Json/JsonAdapter.h>
#include <Engine/Utility/Enum/EnumAdapter.h>
#include <Engine/Utility/Algorithm/Algorithm.h>

//============================================================================
//	AssetDatabase classMethods
//============================================================================

bool Engine::AssetDatabase::Init() {

	// ファイルパスの初期化
	projectRoot_ = "./";
	assetsRoot_ = projectRoot_ / "Engine/Assets";
	libraryRoot_ = projectRoot_ / "Engine/Library";

	// 無ければ作成する
	std::filesystem::create_directories(assetsRoot_);
	std::filesystem::create_directories(libraryRoot_);

	return true;
}

bool Engine::AssetDatabase::RebuildMeta() {

	guidToMeta_.clear();
	pathToGuid_.clear();

	// Assets配下を再帰走査
	for (auto& entry : std::filesystem::recursive_directory_iterator(assetsRoot_)) {

		// ファイルでなければ無視
		if (!entry.is_regular_file()) {
			continue;
		}

		// ファイルのフルパス
		const auto fullPath = entry.path();

		// .metaは無視
		const auto filename = fullPath.filename().string();
		// ファイル名が.metaで終わる、もしくは.meta.を含む場合は無視
		if (Algorithm::EndsWith(filename, ".meta") || filename.find(".meta.") != std::string::npos) {
			continue;
		}

		AssetType guessed = GuessTypeByPath(fullPath);

		// "Assets/..."の相対パス化
		auto rel = std::filesystem::relative(fullPath, projectRoot_);
		std::string assetPath = NormalizePath(rel);
		// インポート
		ImportOrGet(assetPath, guessed);
	}
	return true;
}

Engine::AssetID Engine::AssetDatabase::ImportOrGet(const std::string& assetPath, AssetType guessedType) {

	// 既にインデックスにいるなら返す
	auto it = pathToGuid_.find(assetPath);
	if (it != pathToGuid_.end()) {
		return it->second;
	}

	//  メタデータのフルパスを取得
	std::filesystem::path assetFull = projectRoot_ / assetPath;
	std::filesystem::path metaFull = MetaPathOf(assetFull);

	AssetMeta meta{};
	meta.assetPath = assetPath;
	bool needsSave = false;

	// メタデータが存在するなら読み込む。無ければ新規作成して保存する
	if (std::filesystem::exists(metaFull)) {
		if (!TryLoadMeta(metaFull, meta)) {

			// メタデータが壊れていた時のフォールバック
			meta.guid = UUID::New();
			meta.type = guessedType;
			needsSave = true;
		} else {

			// メタデータは存在するがタイプがUnknownのとき、ファイルパスから推測したタイプで上書きする
			if (meta.type == AssetType::Unknown && guessedType != AssetType::Unknown) {

				meta.type = guessedType;
				needsSave = true;
			}
			meta.assetPath = assetPath;
		}
	} else {

		meta.guid = UUID::New();
		meta.type = guessedType;
		needsSave = true;
	}

	if (needsSave) {
		SaveMeta(metaFull, meta);
	}

	// インデックス登録
	guidToMeta_[meta.guid] = meta;
	pathToGuid_[meta.assetPath] = meta.guid;
	return meta.guid;
}

const Engine::AssetMeta* Engine::AssetDatabase::Find(AssetID id) const {

	auto it = guidToMeta_.find(id);
	return (it == guidToMeta_.end()) ? nullptr : &it->second;
}

const Engine::AssetMeta* Engine::AssetDatabase::FindByPath(const std::string& assetPath) const {

	auto it = pathToGuid_.find(assetPath);
	return (it == pathToGuid_.end()) ? nullptr : Find(it->second);
}

std::filesystem::path Engine::AssetDatabase::ResolveFullPath(AssetID id) const {

	const auto* meta = Find(id);
	if (!meta) {
		return {};
	}
	return projectRoot_ / meta->assetPath;
}

std::string Engine::AssetDatabase::NormalizePath(const std::filesystem::path& path) {

	// パスを正規化して文字列化
	return path.generic_string();
}

std::filesystem::path Engine::AssetDatabase::MetaPathOf(const std::filesystem::path& assetFullPath) {

	return assetFullPath.string() + ".meta";
}

Engine::AssetType Engine::AssetDatabase::GuessTypeByPath(const std::filesystem::path& assetFullPath) const {

	// ファイル名と拡張子を小文字化して取得
	std::string filename = Algorithm::ToLower(assetFullPath.filename().string());
	std::string extension = Algorithm::ToLower(assetFullPath.extension().string());

	// 拡張子から推測
	if (Algorithm::EndsWith(filename, ".scene.json") || extension == ".scene") {
		return AssetType::Scene;
	}
	if (Algorithm::EndsWith(filename, ".prefab.json") || extension == ".prefab") {
		return AssetType::Prefab;
	}
	if (Algorithm::EndsWith(filename, ".material.json") || extension == ".material") {
		return AssetType::Material;
	}
	if (Algorithm::EndsWith(filename, ".shader.json") || extension == ".shader" || extension == ".hlsl" || extension == ".hlsli") {
		return AssetType::Shader;
	}
	if (Algorithm::EndsWith(filename, ".pipeline.json")) {
		return AssetType::RenderPipeline;
	}
	if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".dds") {
		return AssetType::Texture;
	}
	if (extension == ".obj" || extension == ".gltf" || extension == ".glb") {
		return AssetType::Mesh;
	}
	if (Algorithm::EndsWith(filename, ".font.json") || Algorithm::EndsWith(filename, ".msdf.json") ||
		extension == ".ttf" || extension == ".otf" || extension == ".fnt" || extension == ".font") {
		return AssetType::Font;
	}
	return AssetType::Unknown;
}

bool Engine::AssetDatabase::TryLoadMeta(const std::filesystem::path& metaFullPath, AssetMeta& out) const {

	nlohmann::json data = JsonAdapter::Load(metaFullPath.string(), true);

	std::string guidStr = data.value("guid", "");
	std::string typeStr = data.value("type", "Unknown");

	// GUIDが無い、もしくは不正
	if (guidStr.empty()) {
		return false;
	}

	out.guid = FromString16Hex(guidStr);
	out.type = EnumAdapter<AssetType>::FromString(typeStr).value();
	std::filesystem::path assetFullPath = metaFullPath;
	assetFullPath.replace_extension("");
	out.assetPath = NormalizePath(std::filesystem::relative(assetFullPath, projectRoot_));
	return true;
}

bool Engine::AssetDatabase::SaveMeta(const std::filesystem::path& metaFullPath, const AssetMeta& meta) const {

	nlohmann::json data = nlohmann::json::object();

	data["guid"] = ToString(meta.guid);
	data["type"] = std::string(EnumAdapter<AssetType>::ToString(meta.type));
	data["importer"] = "nlohmann::json";
	data["version"] = 1;

	std::ofstream ofs(metaFullPath, std::ios::binary | std::ios::trunc);
	if (!ofs.is_open()) {
		return false;
	}

	// ファイルに書き込む
	ofs << data.dump(2);

	return true;
}