#include "EditorTemplateSnapshot.h"

//============================================================================
//	include
//============================================================================
#include "EditorTemplateStorage.h"
#include <Engine/Editor/Utility/EditorSourceNavigation.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/Database/AssetDependencyScanner.h>
#include <Engine/Core/Assets/Database/AssetDependencyResolver.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/World/Scene/Serialization/SceneDocument.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <array>
#include <deque>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>

namespace {

	using namespace Engine;

	// プロジェクトの共有データだけを収集する
	class SnapshotCollector {
	public:

		SnapshotCollector(const EditorTemplateSaveRequest& request, const AssetDatabase& database) :
			request_(request), database_(database) {}

		// 参照と動的な追加対象を収集する
		void Collect() {

			AddAsset(request_.entry.startupScene);
			AddTree(request_.project / "ProjectSettings", "ProjectSettings", false);
			AddTree(request_.project / "Scripts", "Scripts", true);
			AddScripts();
			CollectScriptReferences();
			for (const auto& path : request_.entry.additionalPaths) {
				const auto relative = Algorithm::PathFromUTF8(path);
				RequireRelative(relative);
				const auto source = request_.project / relative;
				if (!std::filesystem::exists(source)) throw std::runtime_error("追加ファイルが見つかりません: " + path);
				if (std::filesystem::is_directory(source)) AddTree(source, path, false);
				else AddFile(source, path);
			}
			while (!assets_.empty()) {
				const auto id = assets_.front();
				assets_.pop_front();
				const auto* meta = database_.Find(id);
				if (!meta) throw std::runtime_error("参照アセットが見つかりません: " + ToString(id));
				if (meta->assetPath.starts_with("Engine/Assets/")) continue;
				const auto source = database_.ResolveFullPath(id);
				const auto logical = ToDestination(source);
				AddFile(source, logical);
				const auto extension = Algorithm::ToLower(source.extension().string());
				if (meta->type == AssetType::Scene) {
					auto data = Load(source);
					if (!SceneDocument::ValidateSceneFileRoot(data) ||
						(data.contains("ExternalActors") && !SceneDocument::LoadExternalActors(source, id, data)))
						throw std::runtime_error("シーンの保存データを読めません: " + logical);
					data.erase("ExternalActors");
					documents_[logical] = data;
					Scan(data);
				}
				std::vector<AssetDatabaseIssue> issues;
				for (const auto dependency : AssetDependencyResolver::ExtractDependencies(database_, *meta, issues)) AddAsset(dependency);
				if (!issues.empty()) throw std::runtime_error("参照先を解決できません: " + meta->assetPath + " " + issues.front().detail);
				if (meta->type == AssetType::Mesh) {
					std::vector<std::filesystem::path> dependencies;
					std::string diagnostic;
					if (!ModelFileDependencyCollector::Collect(source, dependencies, diagnostic))
						throw std::runtime_error("モデルの付随ファイルを収集できません: " + diagnostic);
					for (const auto& path : dependencies) AddFile(path, ToDestination(path));
				}
				if (extension == ".hlsl" || extension == ".hlsli") AddIncludes(source);
			}
		}

		// 共通雛形へ収集した編集データを重ねる
		void Write(const std::filesystem::path& root) {

			if (!std::filesystem::is_regular_file(request_.repository / "Templates/ManagedProject/Common/Scripts/GameScripts.csproj"))
				throw std::runtime_error("配布用の共通C#テンプレートが見つかりません");
			EditorTemplateStorage::CopyTree(request_.repository / "Templates/ManagedProject/Common", root);
			for (const auto& [logical, source] : files_) {
				if (ContentHash::FileSHA256(source) != revisions_.at(logical))
					throw std::runtime_error("収集中に変更されたファイルがあります 保存し直してください: " + logical);
				const auto target = root / Algorithm::PathFromUTF8(logical);
				std::filesystem::create_directories(target.parent_path());
				std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing);
				std::filesystem::permissions(target, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
				if (ContentHash::FileSHA256(target) != revisions_.at(logical))
					throw std::runtime_error("コピー中に変更されたファイルがあります 保存し直してください: " + logical);
			}
			for (const auto& [logical, data] : documents_) Save(root / Algorithm::PathFromUTF8(logical), data);
			WriteScriptReferences(root);
			WritePackages(root);
			Save(root / "ProjectSettings/Runtime/StartupScene.json", {{"activeScene", ToString(request_.entry.startupScene)}});
			if (!EditorTemplateStorage::WriteEntry(request_.entry, root)) throw std::runtime_error("配布元の設定を保存できません");
			std::map<std::string, std::string> revisions;
			for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
				if (item.is_regular_file() && item.path() != root / "template.json")
					revisions[LogicalPath(item.path().lexically_relative(root))] = ContentHash::FileSHA256(item.path());
			}
			std::string bytes;
			for (const auto& [path, revision] : revisions) bytes += path + "\n" + revision + "\n";
			auto manifest = Load(root / "template.json");
			manifest["contentRevision"] = ContentHash::SHA256(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
			Save(root / "template.json", manifest);
		}

	private:

		const EditorTemplateSaveRequest& request_;
		const AssetDatabase& database_;
		std::deque<AssetID> assets_;
		std::set<AssetID> visited_;
		std::map<std::string, std::filesystem::path> files_;
		std::map<std::string, std::string> revisions_;
		std::map<std::string, nlohmann::json> documents_;
		std::set<std::filesystem::path> shaders_;
		std::string scriptReferences_;

		// 論理パスの区切りをOSによらず揃える
		static std::string LogicalPath(const std::filesystem::path& path) {

			return Algorithm::ConvertString(path.generic_wstring());
		}

		// 配布先から外へ出るパスを拒否する
		static void RequireRelative(const std::filesystem::path& path) {

			if (path.empty() || path.has_root_path()) throw std::runtime_error("配布先の相対パスが不正です");
			for (const auto& part : path) if (part == ".." || part.native().find(L':') != std::wstring::npos)
				throw std::runtime_error("プロジェクトの外にあるファイルは保存できません");
			const auto first = path.begin()->string();
			if (first != "GameAssets" && first != "Scripts" && first != "ProjectSettings" && first != "Packages")
				throw std::runtime_error("追加対象はGameAssets・Scripts・ProjectSettings・Packages内で指定してください");
		}

		// 読み込めないJSONは配布元へ含めない
		static nlohmann::json Load(const std::filesystem::path& source) {

			nlohmann::json data;
			std::string diagnostic;
			if (!JsonFile::TryLoad(source, data, &diagnostic)) throw std::runtime_error(diagnostic);
			return data;
		}

		// 書込失敗を保存操作全体へ伝える
		static void Save(const std::filesystem::path& path, const nlohmann::json& data) {

			std::filesystem::create_directories(path.parent_path());
			if (!JsonFile::Save(path, data)) throw std::runtime_error("配布元の文書を保存できません: " + Algorithm::PathToUTF8(path));
		}

		// Packageを含むファイルの配置先を解決する
		std::string ToDestination(const std::filesystem::path& source) const {

			const auto path = std::filesystem::weakly_canonical(source);
			const auto relative = path.lexically_relative(std::filesystem::weakly_canonical(request_.project));
			if (!relative.empty() && !relative.native().starts_with(L"..")) return LogicalPath(relative);
			for (const auto& package : RuntimePaths::GetPackages()) {
				const auto child = path.lexically_relative(std::filesystem::weakly_canonical(package.root));
				if (!child.empty() && !child.native().starts_with(L"..")) return "Packages/" + package.name + "/" + LogicalPath(child);
			}
			throw std::runtime_error("配布先で解決できない外部ファイルです: " + Algorithm::PathToUTF8(source));
		}

		// 同一アセットの循環参照をまとめる
		void AddAsset(AssetID id) {

			if (id && visited_.insert(id).second) assets_.push_back(id);
		}

		// 本体と付随するmetaを同じ保存先へ収集する
		void AddFile(const std::filesystem::path& source, const std::string& logical) {

			RequireRelative(Algorithm::PathFromUTF8(logical));
			if (std::filesystem::is_symlink(source) || !std::filesystem::is_regular_file(source))
				throw std::runtime_error("保存するファイルを読めません: " + Algorithm::PathToUTF8(source));
			ToDestination(source);
			files_[logical] = source;
			revisions_[logical] = ContentHash::FileSHA256(source);
			if (revisions_[logical].empty()) throw std::runtime_error("ファイルの内容を確認できません: " + logical);
			if (const auto* meta = database_.FindByPath(RuntimePaths::ToAssetPath(source))) AddAsset(meta->guid);
			if (!logical.ends_with(".meta")) {
				const auto metaPath = std::filesystem::path(source.native() + L".meta");
				if (std::filesystem::is_regular_file(metaPath)) {
					if (std::filesystem::is_symlink(metaPath)) throw std::runtime_error("リンクされたmetaは保存できません");
					ToDestination(metaPath);
					files_[logical + ".meta"] = metaPath;
					revisions_[logical + ".meta"] = ContentHash::FileSHA256(metaPath);
				}
			}
		}

		// 生成物を除外してフォルダー内の共有データを収集する
		void AddTree(const std::filesystem::path& source, const std::string& logical, bool scripts) {

			if (!std::filesystem::exists(source)) return;
			for (auto it = std::filesystem::recursive_directory_iterator(source); it != std::filesystem::recursive_directory_iterator{}; ++it) {
				if (it->is_symlink()) throw std::runtime_error("リンクされたファイルは保存できません");
				const auto name = Algorithm::ToLower(it->path().filename().string());
				if (it->is_directory()) {
					if (name == "obj" || name == "bin" || name == ".git" || name == ".vs" || name == "externalactors") it.disable_recursion_pending();
					continue;
				}
				if (!it->is_regular_file() || name.ends_with(".user") || name.ends_with(".csproj") || name.ends_with(".props") ||
					name.ends_with(".targets") || name.ends_with(".tmp") || name.ends_with(".bak")) continue;
				if (scripts && name.ends_with(".assemblyinfo.cs")) continue;
				const auto destination = logical + "/" + LogicalPath(it->path().lexically_relative(source));
				AddFile(it->path(), destination);
				if (it->path().extension() == ".json" && destination != "ProjectSettings/Runtime/StartupScene.json" &&
					!name.ends_with(".meta")) Scan(Load(it->path()));
			}
		}

		// 未参照の補助クラスもゲームスクリプトへ含める
		void AddScripts() {

			const auto source = request_.project / "GameAssets";
			for (auto it = std::filesystem::recursive_directory_iterator(source); it != std::filesystem::recursive_directory_iterator{}; ++it) {
				if (it->is_symlink()) { if (it->is_directory()) it.disable_recursion_pending(); continue; }
				const auto name = Algorithm::ToLower(it->path().filename().string());
				if (it->is_directory() && (name == "obj" || name == "bin" || name == ".git" || name == ".vs")) {
					it.disable_recursion_pending();
					continue;
				}
				if (it->is_regular_file() && Algorithm::ToLower(it->path().extension().string()) == ".cs") AddFile(it->path(), ToDestination(it->path()));
			}
		}

		// NuGetと同梱DLLの参照だけを配布用プロジェクトへ引き継ぐ
		void CollectScriptReferences() {

			std::ifstream input(request_.project / "Scripts/GameScripts.csproj");
			if (!input) return;
			const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			const std::regex nodes(R"(<(PackageReference|Reference|ProjectReference|Import)\b[^>]*(?:/>|>[\s\S]*?</\1>))");
			for (std::sregex_iterator it(text.begin(), text.end(), nodes), end; it != end; ++it) {
				std::string node = it->str();
				if (node.find("NEM.ScriptCore") != std::string::npos || node.find("NEM.ScriptCodeGen") != std::string::npos ||
					node.find("NEM.ScriptAnalyzers") != std::string::npos || node.find("NEM.GameScripts.targets") != std::string::npos ||
					node.find("EngineBinding.props") != std::string::npos || node.find("Sdk=") != std::string::npos) continue;
				const auto type = (*it)[1].str();
				if (type == "ProjectReference" || type == "Import" || node.find("$(") != std::string::npos)
					throw std::runtime_error("配布先で解決できないC#参照があります: " + node);
				if (type == "Reference") {
					std::smatch hint;
					const std::regex hintPattern(R"(<HintPath>\s*([^<]+?)\s*</HintPath>)");
					if (!std::regex_search(node, hint, hintPattern))
						throw std::runtime_error("同梱先を指定できないC#参照があります: " + node);
					const auto path = std::filesystem::weakly_canonical(request_.project / "Scripts" / Algorithm::PathFromUTF8(hint[1].str()));
					const auto destination = ToDestination(path);
					AddFile(path, destination);
					auto relative = LogicalPath(Algorithm::PathFromUTF8(destination).lexically_relative("Scripts"));
					for (size_t at = 0; (at = relative.find('&', at)) != std::string::npos; at += 5) relative.replace(at, 1, "&amp;");
					node.replace(static_cast<size_t>(hint.position()), static_cast<size_t>(hint.length()), "<HintPath>" + relative + "</HintPath>");
				}
				scriptReferences_ += node + "\n";
			}
		}

		// 配布用の共通雛形へ個別の参照を追加する
		void WriteScriptReferences(const std::filesystem::path& root) {

			if (scriptReferences_.empty()) return;
			const auto path = root / "Scripts/GameScripts.csproj";
			std::ifstream input(path, std::ios::binary);
			std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
			input.close();
			const auto end = text.rfind("</Project>");
			if (end == std::string::npos) throw std::runtime_error("配布用のC#雛形が不正です");
			text.insert(end, "<ItemGroup>\n" + scriptReferences_ + "</ItemGroup>\n");
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output << text;
			if (!output) throw std::runtime_error("C#参照を保存できません");
		}

		// 登録済みのIDと論理パスから依存先を追加する
		void Scan(const nlohmann::json& data) {

			AssetDependencyScanner::IDReferences ids;
			AssetDependencyScanner::PathReferences paths;
			AssetDependencyScanner::ScanReferences(data, ids, paths, true);
			for (const auto& [id, type] : ids) if (type != AssetType::Unknown || database_.Find(id)) AddAsset(id);
			for (const auto& [path, type] : paths) {
				if (path.starts_with("Engine/Assets/")) continue;
				if (const auto* meta = database_.FindByPath(path)) AddAsset(meta->guid);
				else if (type == AssetType::Shader) {
					const auto source = database_.ResolveAssetPath(path);
					AddFile(source, ToDestination(source));
					AddIncludes(source);
				}
			}
		}

		// 登録外のShader includeも再帰的に収集する
		void AddIncludes(const std::filesystem::path& source) {

			if (!shaders_.insert(std::filesystem::weakly_canonical(source)).second) return;
			std::ifstream stream(source);
			const std::regex pattern(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
			std::string line;
			while (std::getline(stream, line)) {
				std::smatch match;
				if (!std::regex_search(line, match, pattern)) continue;
				const auto name = Algorithm::PathFromUTF8(match[1].str());
				for (const auto& candidate : std::array{source.parent_path() / name,
					RuntimePaths::GetEngineAssetPath("Rendering") / name, request_.project / "GameAssets/Shaders" / name}) {
					if (!std::filesystem::is_regular_file(candidate)) continue;
					if (!RuntimePaths::ToAssetPath(candidate).starts_with("Engine/Assets/")) AddFile(candidate, ToDestination(candidate));
					AddIncludes(candidate);
					break;
				}
			}
		}

		// Packageの推移依存を埋め込み参照へ揃える
		void WritePackages(const std::filesystem::path& root) {

			if (!RuntimePaths::GetPackageIssues().empty()) throw std::runtime_error("Packageの依存関係を解決してから保存してください");
			nlohmann::json dependencies = nlohmann::json::object();
			for (const auto& package : RuntimePaths::GetPackages()) dependencies[package.name] = package.version;
			for (const auto& package : RuntimePaths::GetPackages()) {
				const auto target = root / "Packages" / package.name;
				EditorTemplateStorage::CopyTree(package.root, target);
				auto manifest = Load(target / "package.json");
				if (manifest.contains("dependencies")) {
					for (auto& [name, value] : manifest["dependencies"].items()) {
						if (!dependencies.contains(name)) throw std::runtime_error("Packageの依存先が不足しています: " + name);
						value = dependencies[name];
					}
					Save(target / "package.json", manifest);
				}
			}
			Save(root / "Packages/manifest.json", {{"schemaVersion", 1}, {"dependencies", dependencies}});
		}
	};
}

Engine::EditorTemplateResult Engine::SaveEditorTemplate(const EditorTemplateSaveRequest& request, const AssetDatabase& database) {

	std::filesystem::path staged;
	try {
		if (!EditorSourceNavigation::IsEngineSourceProject()) return {false, "エンジン開発環境でのみ保存できます"};
		if (!request.entry.startupScene || request.entry.name.empty()) return {false, "保存するシーンと名前を指定してください"};
		const auto target = EditorTemplateStorage::ResolveRoot(request.entry.id);
		std::filesystem::create_directories(target.parent_path());
		staged = target.parent_path() / (".saving-" + ToString(AssetID::New()));
		SnapshotCollector collector(request, database);
		collector.Collect();
		collector.Write(staged);
		EditorTemplateStorage::ReplaceDirectory(staged, target);
		nlohmann::json workspace;
		const auto marker = request.project / "Library/TemplateAuthoring.json";
		if (JsonFile::TryLoad(marker, workspace) && workspace.is_object() && workspace.value("id", std::string{}) == request.entry.id) {
			nlohmann::json manifest;
			if (JsonFile::TryLoad(target / "template.json", manifest)) {
				workspace["sourceRevision"] = manifest.value("contentRevision", std::string{});
				JsonFile::Save(marker, workspace);
			}
		}
		return {true, "テンプレートを保存しました", target};
	} catch (const std::exception& error) {
		if (!staged.empty()) { std::error_code cleanup; std::filesystem::remove_all(staged, cleanup); }
		return {false, error.what()};
	}
}
