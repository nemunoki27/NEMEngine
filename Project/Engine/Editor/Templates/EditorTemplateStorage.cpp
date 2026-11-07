#include "EditorTemplateStorage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Utility/EditorSourceNavigation.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {

	// 識別子をディレクトリ名として検証する
	bool IsSampleID(const std::string& id) {

		return id.size() == 32 && std::all_of(id.begin(), id.end(), [](unsigned char value) {
			return std::isxdigit(value) != 0;
		});
	}
}

std::filesystem::path Engine::EditorTemplateStorage::Repository() {

	return RuntimePaths::GetEngineProjectRoot().parent_path();
}

std::filesystem::path Engine::EditorTemplateStorage::ResolveRoot(const std::string& id) {

	if (id != "2D" && id != "3D" && !IsSampleID(id)) throw std::runtime_error("サンプルの識別子が不正です");
	const auto target = Repository() / ((id == "2D" || id == "3D") ? "Templates/ManagedProject" : "Templates/Samples") / id;
	if (!StorageFileUtility::IsInside(target, Repository())) throw std::runtime_error("配布元はエンジンリポジトリ内へ保存してください");
	return target;
}

bool Engine::EditorTemplateStorage::Read(const std::filesystem::path& root, EditorTemplateEntry& entry) try {

	nlohmann::json data;
	if (!JsonFile::TryLoad(root / "template.json", data) || !data.is_object() || data.value("schemaVersion", 0) != 1) return false;
	entry.id = data.value("id", std::string{});
	entry.kind = data.value("kind", std::string{});
	if ((entry.id != "2D" && entry.id != "3D" && !IsSampleID(entry.id)) ||
		(entry.kind != "2D" && entry.kind != "3D" && entry.kind != "Sample")) return false;
	entry.name = data.value("name", entry.id);
	entry.description = data.value("description", std::string{});
	entry.version = data.value("version", std::string("1.0.0"));
	entry.engineVersion = data.value("engineVersion", std::string{});
	entry.startupScene = ParseAssetID(data, "startupScene");
	entry.additionalPaths = data.value("additionalPaths", std::vector<std::string>{});
	entry.root = root;
	return static_cast<bool>(entry.startupScene);
} catch (const nlohmann::json::exception&) {
	return false;
}

std::vector<Engine::EditorTemplateEntry> Engine::EditorTemplateStorage::ListSamples() {

	std::vector<EditorTemplateEntry> result;
	std::error_code error;
	const auto root = Repository() / "Templates/Samples";
	for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
		if (!it->is_directory(error) || it->is_symlink(error)) continue;
		EditorTemplateEntry entry;
		if (Read(it->path(), entry) && entry.kind == "Sample") result.push_back(std::move(entry));
	}
	std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
	return result;
}

Engine::EditorTemplateEntry Engine::EditorTemplateStorage::MakeEntry(const std::string& kind) {

	EditorTemplateEntry entry;
	entry.id = kind == "Sample" ? ToString(AssetID::New()) : kind;
	entry.kind = kind;
	entry.name = kind == "Sample" ? "サンプルプロジェクト" : kind + "テンプレート";
	entry.root = ResolveRoot(entry.id);
	EditorTemplateEntry previous;
	if (Read(entry.root, previous)) return previous;
	return entry;
}

bool Engine::EditorTemplateStorage::WriteEntry(const EditorTemplateEntry& entry, const std::filesystem::path& root) {

	nlohmann::json data;
	if (!JsonFile::TryLoad(root / "template.json", data) || !data.is_object()) data = nlohmann::json::object();
	data.update(nlohmann::json{
		{"schemaVersion", 1}, {"id", entry.id}, {"name", entry.name}, {"description", entry.description},
		{"kind", entry.kind}, {"version", entry.version}, {"engineVersion", entry.engineVersion},
		{"startupScene", ToString(entry.startupScene)}, {"additionalPaths", entry.additionalPaths}});
	return JsonFile::Save(root / "template.json", data);
}

void Engine::EditorTemplateStorage::CopyTree(const std::filesystem::path& source, const std::filesystem::path& target) {

	if (!std::filesystem::exists(source)) return;
	if (std::filesystem::is_symlink(source)) throw std::runtime_error("リンクされたフォルダーは保存できません");
	std::filesystem::create_directories(target);
	for (const auto& item : std::filesystem::recursive_directory_iterator(source)) {
		if (item.is_symlink()) throw std::runtime_error("リンクされたファイルは保存できません");
		const auto destination = target / item.path().lexically_relative(source);
		if (item.is_directory()) std::filesystem::create_directories(destination);
		else if (item.is_regular_file()) {
			std::filesystem::create_directories(destination.parent_path());
			std::filesystem::copy_file(item.path(), destination, std::filesystem::copy_options::overwrite_existing);
			std::filesystem::permissions(destination, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
		}
	}
}

void Engine::EditorTemplateStorage::ReplaceDirectory(const std::filesystem::path& staged, const std::filesystem::path& target) {

	const auto previous = target.parent_path() / (target.filename().string() + ".previous-" + ToString(AssetID::New()));
	const bool exists = std::filesystem::exists(target);
	if (exists) std::filesystem::rename(target, previous);
	try { std::filesystem::rename(staged, target); }
	catch (...) { if (exists) std::filesystem::rename(previous, target); throw; }
	if (exists) std::filesystem::remove_all(previous);
}

Engine::EditorTemplateResult Engine::EditorTemplateStorage::RemoveSample(const std::string& id) {

	try {
		if (!EditorSourceNavigation::IsEngineSourceProject() || !IsSampleID(id))
			return {false, "エンジン開発環境でのみ削除できます"};
		const auto root = ResolveRoot(id);
		if (std::filesystem::is_symlink(root) || !StorageFileUtility::IsInside(root, Repository() / "Templates/Samples"))
			return {false, "リンクされた保存元は削除できません"};
		std::filesystem::remove_all(root);
		return {true, "サンプルプロジェクトを削除しました", root};
	} catch (const std::exception& error) { return {false, error.what()}; }
}
