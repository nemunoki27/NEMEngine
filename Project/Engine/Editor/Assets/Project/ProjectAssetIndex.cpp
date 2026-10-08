#include "ProjectAssetIndex.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Assets/Database/AssetFileUtility.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <exception>
#include <string_view>
#include <utility>

//============================================================================
//	ProjectAssetIndex classMethods
//============================================================================
namespace {

	// 同じファイル名で特定の拡張子を持つ兄弟ファイルが存在するか
	bool ExistsSiblingWithSameStem(const std::filesystem::path& fullPath, const char* extension) {
		std::filesystem::path sibling = fullPath.parent_path() / fullPath.stem();
		sibling += Engine::Algorithm::PathFromUTF8(extension);
		return std::filesystem::exists(sibling);
	}

	struct AssetRootDesc {

		std::string name;				// 表示するルート名
		std::string virtualPath;		// Project内のルート名
		std::filesystem::path fullPath; // 走査する実フォルダー
	};

	AssetRootDesc MakeAssetRootDesc(const Engine::AssetDatabase& database, Engine::ProjectAssetSource source) {

		switch (source) {
		case Engine::ProjectAssetSource::Engine:
			return {"Engine", "Engine/Assets", database.GetAssetsRoot()};
		case Engine::ProjectAssetSource::Game:
			return {"Game", "GameAssets", Engine::RuntimePaths::GetGameRoot() / "GameAssets"};
		}
		return {"Engine", "Engine/Assets", database.GetAssetsRoot()};
	}

	Engine::AssetType GuessBrowserAssetType(const std::filesystem::path& fullPath) {

		const std::string fileName = Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(fullPath.filename()));
		const std::string extension = Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(fullPath.extension()));

		if (Engine::Algorithm::EndsWith(fileName, ".animclip.json") || extension == ".animclip") {
			return Engine::AssetType::AnimationClip;
		}
		return Engine::AssetType::Unknown;
	}
}

bool Engine::ProjectAssetIndex::Rebuild(const AssetDatabase& database, ProjectAssetSource source) {

	try {
		// 全ての走査が成功してから一覧を差し替える
		ProjectAssetIndex next;
		next.BuildRoot(database, source);
		root_ = std::move(next.root_);
		source_ = next.source_;
		return true;
	} catch (const std::exception& error) {
		Logger::Output(LogType::Engine, spdlog::level::err, "Projectの一覧を更新できません 詳細={}", error.what());
		return false;
	}
}

void Engine::ProjectAssetIndex::BuildRoot(const AssetDatabase& database, ProjectAssetSource source) {

	const AssetRootDesc rootDesc = MakeAssetRootDesc(database, source);
	source_ = source;

	// Assets配下を再帰走査してインデックスを構築
	root_ = {};
	root_.name = rootDesc.name;
	root_.virtualPath = rootDesc.virtualPath;

	if (!std::filesystem::exists(rootDesc.fullPath) || !std::filesystem::is_directory(rootDesc.fullPath)) {
		return;
	}

	for (auto iterator = std::filesystem::recursive_directory_iterator(rootDesc.fullPath);
		iterator != std::filesystem::recursive_directory_iterator{}; ++iterator) {

		const auto& entry = *iterator;

		if (entry.is_directory()) {

			// コピー準備中のファイルをProjectへ公開しない
			if (AssetFileUtility::IsAssetCopyStagingDirectory(entry.path())) {
				iterator.disable_recursion_pending();
				continue;
			}
			std::filesystem::path directory = std::filesystem::relative(entry.path(), rootDesc.fullPath);
			EnsureDirectory(directory);
			continue;
		}
		if (!entry.is_regular_file()) {
			continue;
		}
		const std::filesystem::path fullPath = entry.path();
		if (ShouldHideInBrowser(fullPath)) {
			continue;
		}

		// 論理アセットパス化
		const std::string assetPath = RuntimePaths::ToAssetPath(fullPath);
		if (assetPath.empty()) {
			continue;
		}
		const AssetMeta* meta = database.FindByPath(assetPath);
		if (!meta) {
			continue;
		}

		// ブラウザエントリーの作成
		ProjectAssetEntry browserEntry{};
		browserEntry.assetID = meta->guid;
		browserEntry.type = meta->type == AssetType::Unknown ? GuessBrowserAssetType(fullPath) : meta->type;
		browserEntry.assetPath = assetPath;
		browserEntry.fileName = Algorithm::PathToUTF8(fullPath.filename());
		browserEntry.displayName = MakeDisplayName(fullPath);

		// ディレクトリノードに追加
		std::filesystem::path directory = std::filesystem::relative(fullPath.parent_path(), rootDesc.fullPath);
		ProjectDirectoryNode* dirNode = EnsureDirectory(directory);
		dirNode->assets.emplace_back(std::move(browserEntry));
	}
	// ディレクトリノードをソート
	SortRecursive(root_);
}

const Engine::ProjectDirectoryNode* Engine::ProjectAssetIndex::FindDirectory(const std::string& virtualPath) const {

	return FindRecursive(root_, virtualPath);
}

const Engine::ProjectAssetEntry* Engine::ProjectAssetIndex::FindAssetByPath(const std::string& assetPath) const {

	return FindAssetRecursive(root_, assetPath);
}

bool Engine::ProjectAssetIndex::ShouldHideInBrowser(const std::filesystem::path& fullPath) {

	// ファイル名と拡張子を小文字化して取得
	const std::string fileName = Algorithm::ToLower(Algorithm::PathToUTF8(fullPath.filename()));
	const std::string extension = Algorithm::ToLower(Algorithm::PathToUTF8(fullPath.extension()));

	// .metaファイルは常に非表示
	if (Engine::Algorithm::EndsWith(fileName, ".meta") || fileName.find(".meta.") != std::string::npos) {
		return true;
	}

	// hlsliはinclude専用で直接編集対象にしないので非表示にする
	if (extension == ".hlsli") {
		return true;
	}

	// Fontの生成情報と同名Atlasを非表示にする
	if (Engine::Algorithm::EndsWith(fileName, ".font.json")) {
		return true;
	}
	if (extension == ".png" && ExistsSiblingWithSameStem(fullPath, ".font.json")) {
		return true;
	}

	// 固定の基底文字集合を非表示にする
	if (fileName == "base_charset.txt") {
		return true;
	}

	return false;
}

std::string Engine::ProjectAssetIndex::MakeDisplayName(const std::filesystem::path& fullPath) {

	const std::string fileName = Algorithm::PathToUTF8(fullPath.filename());
	const std::string_view suffix = AssetTypeResolver::FindCompoundSuffix(fullPath);
	if (!suffix.empty()) {
		return fileName.substr(0, fileName.size() - suffix.size());
	}
	// HLSLを含む通常拡張子は省略せず、ファイル種別まで判別できる表示にする
	return fileName;
}

Engine::ProjectDirectoryNode* Engine::ProjectAssetIndex::EnsureDirectory(const std::filesystem::path& relativeDirectory) {

	// 相対パスが空ならルートを返す
	ProjectDirectoryNode* current = &root_;
	if (relativeDirectory.empty() || relativeDirectory == ".") {
		return current;
	}

	// 相対パスの階層ごとにディレクトリを確保する
	std::string currentPath = root_.virtualPath;
	for (const auto& part : relativeDirectory) {

		const std::string name = Algorithm::PathToUTF8(part);
		currentPath += "/" + name;
		auto it = std::find_if(current->children_.begin(), current->children_.end(),
			[&](const std::unique_ptr<ProjectDirectoryNode>& child) { return child->name == name; });

		// 存在しない場合は新規作成して移動、存在する場合はそのノードに移動
		if (it == current->children_.end()) {

			auto child = std::make_unique<ProjectDirectoryNode>();
			child->name = name;
			child->virtualPath = currentPath;
			current->children_.emplace_back(std::move(child));
			current = current->children_.back().get();
		} else {

			current = it->get();
		}
	}
	return current;
}

void Engine::ProjectAssetIndex::SortRecursive(ProjectDirectoryNode& node) {

	// 子ディレクトリを名前順にソート
	std::sort(node.children_.begin(), node.children_.end(),
		[](const std::unique_ptr<ProjectDirectoryNode>& a, const std::unique_ptr<ProjectDirectoryNode>& b) {
			return a->name < b->name;
		});
	// ディレクトリ内のアセットを表示名順にソート
	std::sort(node.assets.begin(), node.assets.end(),
		[](const ProjectAssetEntry& a, const ProjectAssetEntry& b) { return a.displayName < b.displayName; });
	// 子ディレクトリも再帰的にソート
	for (auto& child : node.children_) {

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
	for (const auto& child : node.GetChildren()) {
		if (const ProjectDirectoryNode* found = FindRecursive(child, virtualPath)) {

			return found;
		}
	}
	return nullptr;
}

const Engine::ProjectAssetEntry* Engine::ProjectAssetIndex::FindAssetRecursive(
	const ProjectDirectoryNode& node, const std::string& assetPath) {

	for (const ProjectAssetEntry& asset : node.assets) {
		if (asset.assetPath == assetPath) {
			return &asset;
		}
	}
	for (const auto& child : node.GetChildren()) {
		if (const ProjectAssetEntry* found = FindAssetRecursive(child, assetPath)) {
			return found;
		}
	}
	return nullptr;
}
