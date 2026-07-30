#include "ProjectAssetFileUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <array>
#include <cctype>
#include <fstream>
#include <format>
#include <iterator>
#include <system_error>
#include <vector>

//============================================================================
//	ProjectAssetFileUtility structures
//============================================================================
namespace Engine {

	struct MovedPathPair {
		std::filesystem::path from;
		std::filesystem::path to;
	};

} // Engine

//============================================================================
//	ProjectAssetFileUtility classMethods
//============================================================================

const char* Engine::ProjectAssetFileUtility::GetCreateMenuLabel(ProjectAssetFileKind kind) {

	switch (kind) {
	case ProjectAssetFileKind::Folder: return "Folder";
	case ProjectAssetFileKind::Text: return "Text File";
	case ProjectAssetFileKind::Script: return "C# Script";
	case ProjectAssetFileKind::Scene: return "Scene";
	case ProjectAssetFileKind::Prefab: return "Prefab";
	case ProjectAssetFileKind::Material: return "Material";
	case ProjectAssetFileKind::AnimationClip: return "AnimationClip";
	case ProjectAssetFileKind::Shader: return "Shader";
	case ProjectAssetFileKind::RenderPipeline: return "Render Pipeline";
	}
	return "Asset";
}

const char* Engine::ProjectAssetFileUtility::GetDefaultName(ProjectAssetFileKind kind) {

	switch (kind) {
	case ProjectAssetFileKind::Folder: return "New Folder";
	case ProjectAssetFileKind::Text: return "New Text";
	case ProjectAssetFileKind::Script: return "NewScript";
	case ProjectAssetFileKind::Scene: return "NewScene";
	case ProjectAssetFileKind::Prefab: return "NewPrefab";
	case ProjectAssetFileKind::Material: return "NewMaterial";
	case ProjectAssetFileKind::AnimationClip: return "NewAnimation";
	case ProjectAssetFileKind::Shader: return "NewShader";
	case ProjectAssetFileKind::RenderPipeline: return "NewPipeline";
	}
	return "NewAsset";
}

std::string Engine::ProjectAssetFileUtility::GetEditableAssetName(const ProjectAssetEntry& asset) {
	// 拡張子を取り除いた、編集可能な名前部分を抽出
	return SplitAssetFileName(std::filesystem::path(asset.assetPath)).first;
}

std::string Engine::ProjectAssetFileUtility::GetProtectedAssetSuffix(const ProjectAssetEntry& asset) {
	// 変更不可能なアセット固有の拡張子部分を抽出
	return SplitAssetFileName(std::filesystem::path(asset.assetPath)).second;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::Create(ProjectAssetSource source,
	const std::string& directoryVirtualPath, ProjectAssetFileKind kind, const std::string& requestedName) {

	ProjectAssetFileResult result{};
	result.isDirectory = kind == ProjectAssetFileKind::Folder;

	// 仮想パスから実ディレクトリを特定し失敗なら中断
	const std::filesystem::path directory = ResolveVirtualDirectory(source, directoryVirtualPath);
	if (directory.empty()) {
		result.message = "Invalid directory path.";
		return result;
	}

	// ディレクトリがなければ作成
	std::error_code ec;
	std::filesystem::create_directories(directory, ec);
	if (ec) {
		result.message = "Failed to create directory.";
		return result;
	}

	// 拡張子の決定とファイル名のサニタイズ
	const char* suffix = GetFileSuffix(kind);
	std::string baseName = SanitizeFileName(requestedName.empty() ? GetDefaultName(kind) : requestedName);
	if (kind != ProjectAssetFileKind::Folder) {
		baseName = RemoveTypedSuffix(baseName, suffix);
	}

	// 同一名称がある場合は自動的に連番を付与して一意のパスを作成
	const std::filesystem::path preferredPath = kind == ProjectAssetFileKind::Folder ?
		directory / baseName :
		directory / (baseName + suffix);
	const std::filesystem::path createPath = MakeUniquePath(preferredPath);
	if (createPath.empty()) {
		result.message = "Failed to build unique file path.";
		return result;
	}

	// フォルダまたはファイルの作成
	if (kind == ProjectAssetFileKind::Folder) {
		std::filesystem::create_directories(createPath, ec);
		if (ec) {
			result.message = "Failed to create folder.";
			return result;
		}
	}
	else {
		// 種類に応じた雛形内容を書き込み
		if (!WriteTextFile(createPath, BuildFileContent(kind, baseName))) {
			result.message = "Failed to write asset file.";
			return result;
		}
	}

	result.success = true;
	result.fullPath = createPath;
	result.assetPath = ToAssetPath(createPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DuplicateAsset(const ProjectAssetEntry& asset) {

	ProjectAssetFileResult result{};

	// 元のアセットパスを解決し存在しなければ中断
	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) {
		result.message = "Source asset was not found.";
		return result;
	}

	// 複製先のパスを既存アセットとの競合回避で決定する
	const std::filesystem::path targetPath = MakeUniquePath(sourcePath);
	if (targetPath.empty()) {
		result.message = "Failed to build duplicate file path.";
		return result;
	}

	// ファイルをコピー
	std::error_code ec;
	std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::none, ec);
	if (ec) {
		result.message = "Failed to copy asset file.";
		return result;
	}

	// 複製されたアセット内部の表示名をファイル名に合わせる
	PatchDuplicatedJsonAsset(targetPath, asset.type);

	// モデルのbin等のサイドカーファイルを合わせてコピーする、.metaは新規発行する
	for (const std::string& sidecar : asset.sidecarFiles) {

		const std::filesystem::path sidecarSource = sourcePath.parent_path() / sidecar;
		if (!std::filesystem::exists(sidecarSource)) {
			continue;
		}

		const std::filesystem::path sidecarTarget =
			targetPath.parent_path() / (targetPath.stem().string() + sidecarSource.extension().string());
		std::filesystem::copy_file(sidecarSource, sidecarTarget, std::filesystem::copy_options::none, ec);
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::CopyAsset(const ProjectAssetEntry& asset,
	ProjectAssetSource targetSource, const std::string& targetDirectoryVirtualPath) {

	ProjectAssetFileResult result{};

	// 元のアセットパスとコピー先ディレクトリを解決する
	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	const std::filesystem::path targetDirectory = ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath) || targetDirectory.empty()) {
		result.message = "Source asset or target folder was not found.";
		return result;
	}

	// コピー先ディレクトリを確保する
	std::error_code ec;
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "Failed to create target folder.";
		return result;
	}

	// コピー先のパスを既存アセットとの競合回避で決定する
	const std::filesystem::path targetPath = MakeUniquePath(targetDirectory / sourcePath.filename());
	if (targetPath.empty()) {
		result.message = "Failed to build copy file path.";
		return result;
	}

	// ファイルをコピー
	std::filesystem::copy_file(sourcePath, targetPath, std::filesystem::copy_options::none, ec);
	if (ec) {
		result.message = "Failed to copy asset file.";
		return result;
	}

	// コピーされたアセット内部の表示名をファイル名に合わせる
	PatchDuplicatedJsonAsset(targetPath, asset.type);

	// モデルのbin等のサイドカーファイルを合わせてコピーする、.metaは新規発行する
	for (const std::string& sidecar : asset.sidecarFiles) {

		const std::filesystem::path sidecarSource = sourcePath.parent_path() / sidecar;
		if (!std::filesystem::exists(sidecarSource)) {
			continue;
		}

		const std::filesystem::path sidecarTarget =
			targetPath.parent_path() / (targetPath.stem().string() + sidecarSource.extension().string());
		std::filesystem::copy_file(sidecarSource, sidecarTarget, std::filesystem::copy_options::none, ec);
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::RenameAsset(const ProjectAssetEntry& asset,
	const std::string& requestedName) {

	ProjectAssetFileResult result{};

	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) {
		result.message = "Source asset was not found.";
		return result;
	}

	// 新しいファイル名の決定
	const auto [currentBaseName, suffix] = SplitAssetFileName(sourcePath);
	std::string baseName = SanitizeFileName(requestedName.empty() ? currentBaseName : requestedName);
	baseName = RemoveTypedSuffix(baseName, suffix.c_str());
	if (baseName.empty()) {
		baseName = currentBaseName;
	}

	const std::filesystem::path targetPath = sourcePath.parent_path() / (baseName + suffix);
	// 変更がないなら成功扱い
	if (targetPath == sourcePath) {
		result.success = true;
		result.fullPath = sourcePath;
		result.assetPath = asset.assetPath;
		return result;
	}
	if (std::filesystem::exists(targetPath)) {
		result.message = "Target asset already exists.";
		return result;
	}

	const std::filesystem::path sourceMetaPath = MakeMetaPath(sourcePath);
	const std::filesystem::path targetMetaPath = MakeMetaPath(targetPath);
	if (std::filesystem::exists(targetMetaPath)) {
		result.message = "Target meta file already exists.";
		return result;
	}

	// 移動履歴でエラー時に元に戻すために使用
	std::vector<MovedPathPair> moved;
	auto rollback = [&moved]() {
		std::error_code rollbackEc;
		for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
			std::filesystem::rename(it->to, it->from, rollbackEc);
		}
		};

	std::error_code ec;
	std::filesystem::rename(sourcePath, targetPath, ec);
	if (ec) {
		result.message = "Failed to rename asset file.";
		return result;
	}
	moved.emplace_back(MovedPathPair{ sourcePath, targetPath });

	// メタファイルも連動してリネーム
	if (std::filesystem::exists(sourceMetaPath)) {
		ec.clear();
		std::filesystem::rename(sourceMetaPath, targetMetaPath, ec);
		if (ec) {
			rollback();
			result.message = "Failed to rename asset meta file.";
			return result;
		}
		moved.emplace_back(MovedPathPair{ sourceMetaPath, targetMetaPath });
	}

	// その他サイドカーファイルもリネーム
	for (const std::string& sidecar : asset.sidecarFiles) {

		const std::filesystem::path sidecarSource = sourcePath.parent_path() / sidecar;
		if (!std::filesystem::exists(sidecarSource)) {
			continue;
		}

		const std::filesystem::path sidecarTarget =
			targetPath.parent_path() / (targetPath.stem().string() + sidecarSource.extension().string());

		ec.clear();
		std::filesystem::rename(sidecarSource, sidecarTarget, ec);
		if (ec) {
			rollback();
			result.message = "Failed to rename asset sidecar file.";
			return result;
		}
		moved.emplace_back(MovedPathPair{ sidecarSource, sidecarTarget });
	}

	// アセット内部の名前定義を新ファイル名に合わせて更新
	PatchRenamedJsonAsset(targetPath, asset.type);

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::RenameDirectory(ProjectAssetSource source,
	const std::string& directoryVirtualPath, const std::string& requestedName) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ResolveVirtualDirectory(source, directoryVirtualPath);
	const std::filesystem::path rootPath = GetSourceRoot(source);
	// ルートフォルダ自身はリネーム禁止
	if (sourcePath.empty() || sourcePath == rootPath ||
		!std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
		result.message = "Source folder was not found or cannot be renamed.";
		return result;
	}

	// フォルダ名にも使えない文字は除去する、空になったら元の名前を維持する
	std::string baseName = SanitizeFileName(requestedName.empty() ? sourcePath.filename().string() : requestedName);
	if (baseName.empty()) {
		baseName = sourcePath.filename().string();
	}

	const std::filesystem::path targetPath = sourcePath.parent_path() / baseName;
	// 変更がないなら成功扱い
	if (targetPath == sourcePath) {
		result.success = true;
		result.fullPath = sourcePath;
		result.assetPath = directoryVirtualPath;
		return result;
	}
	if (std::filesystem::exists(targetPath)) {
		result.message = "Target folder already exists.";
		return result;
	}

	// フォルダ名変更はアセットのGUID参照に影響しない、.meta内のGUIDで参照されるためRebuildで再解決される
	std::error_code ec;
	std::filesystem::rename(sourcePath, targetPath, ec);
	if (ec) {
		result.message = "Failed to rename folder.";
		return result;
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DuplicateDirectory(ProjectAssetSource source,
	const std::string& directoryVirtualPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ResolveVirtualDirectory(source, directoryVirtualPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
		result.message = "Source folder was not found.";
		return result;
	}

	const std::filesystem::path targetPath = MakeUniquePath(sourcePath);
	if (targetPath.empty()) {
		result.message = "Failed to build duplicate folder path.";
		return result;
	}

	std::error_code ec;
	std::filesystem::create_directories(targetPath, ec);
	if (ec) {
		result.message = "Failed to create duplicate folder.";
		return result;
	}

	// フォルダ内を再帰的にコピー
	for (const auto& entry : std::filesystem::recursive_directory_iterator(sourcePath, ec)) {
		if (ec) {
			result.message = "Failed to scan source folder.";
			return result;
		}

		const std::filesystem::path relative = std::filesystem::relative(entry.path(), sourcePath, ec);
		if (ec || !IsSafeRelativePath(relative)) {
			continue;
		}

		const std::filesystem::path destination = targetPath / relative;
		if (entry.is_directory()) {
			std::filesystem::create_directories(destination, ec);
		}
		else if (entry.is_regular_file() && !ShouldSkipCopyFile(entry.path())) {
			std::filesystem::create_directories(destination.parent_path(), ec);
			std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::none, ec);
		}
		if (ec) {
			result.message = "Failed to copy folder contents.";
			return result;
		}
	}

	// コピーされた全アセットの名前とGUIDを修正
	PatchDuplicatedDirectoryAssets(targetPath);

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DeleteAsset(const ProjectAssetEntry& asset) {

	ProjectAssetFileResult result{};

	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) {
		result.message = "Source asset was not found.";
		return result;
	}

	// アセット本体を削除
	std::error_code ec;
	std::filesystem::remove(sourcePath, ec);
	if (ec) {
		result.message = "Failed to delete asset file.";
		return result;
	}

	// メタファイル等のサイドカーファイルも削除
	for (const std::filesystem::path& sidecar : BuildAssetSidecarPaths(asset, sourcePath)) {
		if (std::filesystem::exists(sidecar)) {
			std::filesystem::remove(sidecar, ec);
		}
	}

	result.success = true;
	result.assetPath = asset.assetPath;
	result.fullPath = sourcePath;
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DeleteDirectory(ProjectAssetSource source,
	const std::string& directoryVirtualPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ResolveVirtualDirectory(source, directoryVirtualPath);
	const std::filesystem::path rootPath = GetSourceRoot(source);
	// ルートフォルダ自身は削除禁止
	if (sourcePath.empty() || sourcePath == rootPath || !std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
		result.message = "Source folder was not found or cannot be deleted.";
		return result;
	}

	// フォルダツリーを再帰的に全て削除
	std::error_code ec;
	std::filesystem::remove_all(sourcePath, ec);
	if (ec) {
		result.message = "Failed to delete folder.";
		return result;
	}

	result.success = true;
	result.assetPath = ToAssetPath(sourcePath.parent_path());
	result.fullPath = sourcePath;
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::MoveAsset(const ProjectAssetEntry& asset,
	ProjectAssetSource targetSource, const std::string& targetDirectoryVirtualPath) {

	ProjectAssetFileResult result{};

	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	const std::filesystem::path targetDirectory = ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath) || targetDirectory.empty()) {
		result.message = "Source asset or target folder was not found.";
		return result;
	}

	// 移動先ディレクトリの確保
	std::error_code ec;
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "Failed to create target folder.";
		return result;
	}

	// 移動後のパスを重複回避で決定する
	const std::filesystem::path targetPath = MakeUniquePath(targetDirectory / sourcePath.filename());
	if (targetPath.empty()) {
		result.message = "Failed to build move target path.";
		return result;
	}

	std::vector<MovedPathPair> moved;
	auto rollback = [&moved]() {
		std::error_code rollbackEc;
		for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
			std::filesystem::rename(it->to, it->from, rollbackEc);
		}
		};

	// ファイル移動の実行
	std::filesystem::rename(sourcePath, targetPath, ec);
	if (ec) {
		result.message = "Failed to move asset file.";
		return result;
	}
	moved.emplace_back(MovedPathPair{ sourcePath, targetPath });

	// サイドカーファイルの移動でメタファイルの名前変更も含む
	for (const std::filesystem::path& sidecarSource : BuildAssetSidecarPaths(asset, sourcePath)) {
		if (!std::filesystem::exists(sidecarSource)) {
			continue;
		}

		const std::filesystem::path sidecarTarget = sidecarSource == MakeMetaPath(sourcePath) ?
			MakeMetaPath(targetPath) :
			targetPath.parent_path() / sidecarSource.filename();

		ec.clear();
		std::filesystem::rename(sidecarSource, sidecarTarget, ec);
		if (ec) {
			rollback();
			result.message = "Failed to move asset sidecar file.";
			return result;
		}
		moved.emplace_back(MovedPathPair{ sidecarSource, sidecarTarget });
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::MoveDirectory(ProjectAssetSource source,
	const std::string& sourceDirectoryVirtualPath, const std::string& targetDirectoryVirtualPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ResolveVirtualDirectory(source, sourceDirectoryVirtualPath);
	const std::filesystem::path targetDirectory = ResolveVirtualDirectory(source, targetDirectoryVirtualPath);
	const std::filesystem::path rootPath = GetSourceRoot(source);
	
	// 移動元と移動先の検証でルートフォルダの移動や自分自身への移動は禁止
	if (sourcePath.empty() || targetDirectory.empty() || sourcePath == rootPath ||
		!std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
		result.message = "Source or target folder was not found.";
		return result;
	}
	if (IsSameOrChildPath(targetDirectory, sourcePath)) {
		result.message = "Cannot move a folder into itself.";
		return result;
	}

	std::error_code ec;
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "Failed to create target folder.";
		return result;
	}

	const std::filesystem::path targetPath = MakeUniquePath(targetDirectory / sourcePath.filename());
	if (targetPath.empty()) {
		result.message = "Failed to build move target path.";
		return result;
	}

	// ディレクトリ全体の移動
	std::filesystem::rename(sourcePath, targetPath, ec);
	if (ec) {
		result.message = "Failed to move folder.";
		return result;
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::ImportExternalFile(ProjectAssetSource targetSource,
	const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalFilePath) {

	ProjectAssetFileResult result{};

	std::error_code ec;
	// ディレクトリや存在しないものは取り込まない
	if (externalFilePath.empty() || !std::filesystem::exists(externalFilePath, ec) ||
		std::filesystem::is_directory(externalFilePath, ec)) {
		result.message = "Dropped path is not a file.";
		return result;
	}

	// 取り込み先ディレクトリを解決して確保する
	const std::filesystem::path targetDirectory = ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
	if (targetDirectory.empty()) {
		result.message = "Target folder was not found.";
		return result;
	}
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "Failed to create target folder.";
		return result;
	}

	// 既存アセットとの競合回避でコピー先を決めて取り込む、.metaはRebuildで自動発番される
	const std::filesystem::path targetPath = MakeUniquePath(targetDirectory / externalFilePath.filename());
	if (targetPath.empty()) {
		result.message = "Failed to build import file path.";
		return result;
	}
	std::filesystem::copy_file(externalFilePath, targetPath, std::filesystem::copy_options::none, ec);
	if (ec) {
		result.message = "Failed to copy dropped file.";
		return result;
	}

	result.success = true;
	result.fullPath = targetPath;
	result.assetPath = ToAssetPath(targetPath);
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::ImportExternalDirectory(ProjectAssetSource targetSource,
	const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalDirectoryPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	std::error_code ec;
	// フォルダ以外や存在しないものは取り込まない
	if (externalDirectoryPath.empty() || !std::filesystem::is_directory(externalDirectoryPath, ec)) {
		result.message = "Dropped path is not a folder.";
		return result;
	}

	// 取り込み先ディレクトリを解決して確保する
	const std::filesystem::path targetDirectory = ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
	if (targetDirectory.empty()) {
		result.message = "Target folder was not found.";
		return result;
	}
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "Failed to create target folder.";
		return result;
	}

	// ドロップしたフォルダ名で取り込み先に新フォルダを作る、既存と衝突したら連番にする
	const std::filesystem::path destinationRoot = MakeUniquePath(targetDirectory / externalDirectoryPath.filename());
	if (destinationRoot.empty()) {
		result.message = "Failed to build import folder path.";
		return result;
	}
	std::filesystem::create_directories(destinationRoot, ec);
	if (ec) {
		result.message = "Failed to create imported folder.";
		return result;
	}

	// 中身を再帰的にコピーする、.meta等のサイドカーはRebuildで再発番させるためスキップする
	for (const auto& entry : std::filesystem::recursive_directory_iterator(externalDirectoryPath, ec)) {
		if (ec) {
			result.message = "Failed to scan dropped folder.";
			return result;
		}

		const std::filesystem::path relative = std::filesystem::relative(entry.path(), externalDirectoryPath, ec);
		if (ec || !IsSafeRelativePath(relative)) {
			continue;
		}

		const std::filesystem::path destination = destinationRoot / relative;
		if (entry.is_directory()) {
			std::filesystem::create_directories(destination, ec);
		}
		else if (entry.is_regular_file() && !ShouldSkipCopyFile(entry.path())) {
			std::filesystem::create_directories(destination.parent_path(), ec);
			std::filesystem::copy_file(entry.path(), destination, std::filesystem::copy_options::none, ec);
		}
		if (ec) {
			result.message = "Failed to copy dropped folder contents.";
			return result;
		}
	}

	result.success = true;
	result.fullPath = destinationRoot;
	result.assetPath = ToAssetPath(destinationRoot);
	return result;
}
