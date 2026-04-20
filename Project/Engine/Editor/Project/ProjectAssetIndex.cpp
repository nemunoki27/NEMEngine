#include "ProjectAssetIndex.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Algorithm/Algorithm.h>

//============================================================================
//	ProjectAssetIndex classMethods
//============================================================================

namespace {

	// 同じファイル名で特定の拡張子を持つ兄弟ファイルが存在するか
	static bool ExistsSiblingWithSameStem(const std::filesystem::path& fullPath, const char* extension) {
		auto sibling = fullPath.parent_path() / (fullPath.stem().string() + extension);
		return std::filesystem::exists(sibling);
	}
}

bool Engine::ProjectAssetIndex::Rebuild(const AssetDatabase& database) {

	// Assets配下を再帰走査してインデックスを構築
	root_ = {};
	root_.name = "Engine/Assets";
	root_.virtualPath = "Engine/Assets";
	for (const auto& entry : std::filesystem::recursive_directory_iterator(database.GetAssetsRoot())) {

		if (!entry.is_regular_file()) {
			continue;
		}
		const std::filesystem::path fullPath = entry.path();
		if (ShouldHideInBrowser(fullPath)) {
			continue;
		}

		// "Assets/..."の相対パス化
		const std::string assetPath = std::filesystem::relative(fullPath, database.GetProjectRoot()).generic_string();
		const AssetMeta* meta = database.FindByPath(assetPath);
		if (!meta) {
			continue;
		}

		// ブラウザエントリーの作成
		ProjectAssetEntry browserEntry{};
		browserEntry.assetID = meta->guid;
		browserEntry.type = meta->type;
		browserEntry.assetPath = assetPath;
		browserEntry.fileName = fullPath.filename().string();
		browserEntry.displayName = MakeDisplayName(fullPath);
		browserEntry.sidecarFiles = CollectSidecars(fullPath);

		// ディレクトリノードに追加
		std::filesystem::path directory = std::filesystem::relative(fullPath.parent_path(), database.GetAssetsRoot());
		ProjectDirectoryNode* dirNode = EnsureDirectory(directory);
		dirNode->assets.emplace_back(std::move(browserEntry));
	}
	// ディレクトリノードをソート
	SortRecursive(root_);
	return true;
}

const Engine::ProjectDirectoryNode* Engine::ProjectAssetIndex::FindDirectory(const std::string& virtualPath) const {

	return FindRecursive(root_, virtualPath);
}

bool Engine::ProjectAssetIndex::ShouldHideInBrowser(const std::filesystem::path& fullPath) {

	// ファイル名と拡張子を小文字化して取得
	const std::string fileName = Algorithm::ToLower(fullPath.filename().string());
	const std::string extension = Algorithm::ToLower(fullPath.extension().string());

	// .meta ファイルは常に非表示
	if (Engine::Algorithm::EndsWith(fileName, ".meta") || fileName.find(".meta.") != std::string::npos) {
		return true;
	}

	// .objの付属ファイル
	if (extension == ".mtl" && ExistsSiblingWithSameStem(fullPath, ".obj")) {
		return true;
	}
	// .gltf/.glbの付属バイナリ
	if (extension == ".bin" && (ExistsSiblingWithSameStem(fullPath, ".gltf") || ExistsSiblingWithSameStem(fullPath, ".glb"))) {
		return true;
	}
	return false;
}

std::vector<std::string> Engine::ProjectAssetIndex::CollectSidecars(const std::filesystem::path& fullPath) {

	std::vector<std::string> result;
	const std::string extension = Algorithm::ToLower(fullPath.extension().string());
	// .objの付属ファイル
	if (extension == ".obj") {

		auto mtl = fullPath.parent_path() / (fullPath.stem().string() + ".mtl");
		if (std::filesystem::exists(mtl)) {
			result.emplace_back(mtl.filename().string());
		}
	}
	// .gltf/.glbの付属バイナリ
	if (extension == ".gltf" || extension == ".glb") {

		auto bin = fullPath.parent_path() / (fullPath.stem().string() + ".bin");
		if (std::filesystem::exists(bin)) {
			result.emplace_back(bin.filename().string());
		}
	}
	return result;
}

std::string Engine::ProjectAssetIndex::MakeDisplayName(const std::filesystem::path& fullPath) {

	std::string fileName = fullPath.filename().string();
	std::string lower = Algorithm::ToLower(fileName); 

	// 特定の拡張子を持つファイルは、拡張子を除いたファイル名を表示名とする
	if (Engine::Algorithm::EndsWith(lower, ".scene.json")) {
		return fileName.substr(0, fileName.size() - 5);
	}
	if (Engine::Algorithm::EndsWith(lower, ".prefab.json")) {
		return fileName.substr(0, fileName.size() - 5);
	}
	if (Engine::Algorithm::EndsWith(lower, ".material.json")) {
		return fileName.substr(0, fileName.size() - 5);
	}
	if (Engine::Algorithm::EndsWith(lower, ".shader.json")) {
		return fileName.substr(0, fileName.size() - 5);
	}
	if (Engine::Algorithm::EndsWith(lower, ".pipeline.json")) {
		return fileName.substr(0, fileName.size() - 5);
	}
	if (Engine::Algorithm::EndsWith(lower, ".graph.json")) {
		return fileName.substr(0, fileName.size() - 5);
	}
	return fileName;
}

Engine::ProjectDirectoryNode* Engine::ProjectAssetIndex::EnsureDirectory(const std::filesystem::path& relativeDirectory) {

	// ルートが"Assets"なので、相対パスが空もしくは"."の場合はルートを返す
	ProjectDirectoryNode* current = &root_;
	if (relativeDirectory.empty() || relativeDirectory == ".") {
		return current;
	}

	// 相対パスを"/"区切りで分割して順にディレクトリノードをたどる。存在しない場合は新規作成する
	std::string currentPath = "Engine/Assets";
	for (const auto& part : relativeDirectory) {

		const std::string name = part.string();
		currentPath += "/" + name;
		auto it = std::find_if(current->children.begin(), current->children.end(),
			[&](const std::unique_ptr<ProjectDirectoryNode>& child) {
				return child->name == name;
			});

		// 存在しない場合は新規作成して移動、存在する場合はそのノードに移動
		if (it == current->children.end()) {

			auto child = std::make_unique<ProjectDirectoryNode>();
			child->name = name;
			child->virtualPath = currentPath;
			current->children.emplace_back(std::move(child));
			current = current->children.back().get();
		} else {

			current = it->get();
		}
	}
	return current;
}

void Engine::ProjectAssetIndex::SortRecursive(ProjectDirectoryNode& node) {

	// 子ディレクトリを名前順にソート
	std::sort(node.children.begin(), node.children.end(),
		[](const std::unique_ptr<ProjectDirectoryNode>& a, const std::unique_ptr<ProjectDirectoryNode>& b) {
			return a->name < b->name;
		});
	// ディレクトリ内のアセットを表示名順にソート
	std::sort(node.assets.begin(), node.assets.end(),
		[](const ProjectAssetEntry& a, const ProjectAssetEntry& b) {
			return a.displayName < b.displayName;
		});
	// 子ディレクトリも再帰的にソート
	for (auto& child : node.children) {

		SortRecursive(*child);
	}
}

const Engine::ProjectDirectoryNode* Engine::ProjectAssetIndex::FindRecursive(
	const ProjectDirectoryNode& node, const std::string& virtualPath) {

	// 仮想パスが一致する場合はそのノードを返す
	if (node.virtualPath == virtualPath) {
		return &node;
	}
	// 子ディレクトリを再帰的に検索
	for (const auto& child : node.children) {
		if (const ProjectDirectoryNode* found = FindRecursive(*child, virtualPath)) {

			return found;
		}
	}
	return nullptr;
}