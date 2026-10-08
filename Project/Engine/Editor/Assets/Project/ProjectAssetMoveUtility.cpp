#include "ProjectAssetMoveUtility.h"
#include "ProjectAssetMoveTransaction.h"
#include "ProjectAssetPath.h"
#include "ProjectAssetDocumentPatch.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>

// c++
#include <system_error>

//============================================================================
//	ProjectAssetMoveUtility classMethods
//============================================================================
Engine::ProjectAssetFileResult Engine::ProjectAssetMoveUtility::RenameAsset(
	const ProjectAssetEntry& asset, const std::string& requestedName) {

	ProjectAssetFileResult result{};

	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) {
		result.message = "移動元のAssetが見つかりません";
		return result;
	}

	// 新しいファイル名の決定
	const auto [currentBaseName, suffix] = ProjectAssetPath::SplitAssetFileName(sourcePath);
	std::string baseName = ProjectAssetPath::SanitizeFileName(requestedName.empty() ? currentBaseName : requestedName);
	baseName = ProjectAssetPath::RemoveTypedSuffix(baseName, suffix.c_str());
	if (baseName.empty()) {
		baseName = currentBaseName;
	}

	const std::filesystem::path targetPath = sourcePath.parent_path() / Algorithm::PathFromUTF8(baseName + suffix);
	// 変更がないなら成功扱い
	if (targetPath == sourcePath) {
		result.success = true;
		result.fullPath = sourcePath;
		result.assetPath = asset.assetPath;
		return result;
	}
	if (std::filesystem::exists(targetPath)) {
		result.message = "変更先のAssetが既に存在します";
		return result;
	}

	const std::filesystem::path sourceMetaPath = ProjectAssetPath::MakeMetaPath(sourcePath);
	const std::filesystem::path targetMetaPath = ProjectAssetPath::MakeMetaPath(targetPath);
	if (std::filesystem::exists(targetMetaPath)) {
		result.message = "変更先のmetaが既に存在します";
		return result;
	}

	// 全ての移動先を確定してから改名する
	ProjectAssetMoveTransaction transaction;
	if (!transaction.Add(sourcePath, targetPath)) {
		result.message = "Assetの改名計画を作成できません";
		return result;
	}

	// メタファイルも連動してリネーム
	if (std::filesystem::exists(sourceMetaPath)) {
		if (!transaction.Add(sourceMetaPath, targetMetaPath)) {
			result.message = "metaの改名先が重複しています";
			return result;
		}
	}

	// その他サイドカーファイルもリネーム
	for (const std::string& sidecar : asset.sidecarFiles) {
		// モデルの共有ファイルは本体と一緒に改名しない
		if (ModelDocumentReferences::IsDocumentPath(sourcePath)) {
			break;
		}

		const std::filesystem::path sidecarSource = sourcePath.parent_path() / Algorithm::PathFromUTF8(sidecar);
		if (!std::filesystem::exists(sidecarSource)) {
			continue;
		}

		const std::filesystem::path sidecarTarget = ProjectAssetPath::MakeSiblingPath(targetPath, sidecarSource.extension());

		if (!transaction.Add(sidecarSource, sidecarTarget)) {
			result.message = "付随ファイルの改名先が重複しています";
			return result;
		}
	}
	// 補完Materialの参照も改名前に確定する
	if (ModelDocumentReferences::IsDocumentPath(sourcePath) &&
		!transaction.Prepare(
			0,
			[&](std::string& bytes) { return ModelDocumentReferences::Rebase(sourcePath, targetPath, bytes, result.message); },
			result.message)) {
		return result;
	}
	// 表示名の編集も移動前に準備する
	if (AssetTypeResolver::IsJsonAssetFile(asset.type, targetPath) &&
		!transaction.Prepare(
			0,
			[&](std::string& bytes) { return ProjectAssetDocumentPatch::PrepareJsonAssetName(targetPath, asset.type, bytes); },
			result.message)) {
		return result;
	}
	// 結果の文字列も移動前に確保する
	result.fullPath = targetPath;
	result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
	if (!transaction.Execute(result.message)) {
		if (!transaction.Rollback()) {
			result.message += " 元のファイルへ戻せません。ログを確認してください";
		}
		return result;
	}

	result.success = true;
	transaction.Commit();
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetMoveUtility::RenameDirectory(
	ProjectAssetSource source, const std::string& directoryVirtualPath, const std::string& requestedName) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ProjectAssetPath::ResolveVirtualDirectory(source, directoryVirtualPath);
	const std::filesystem::path rootPath = ProjectAssetPath::GetSourceRoot(source);
	// ルートフォルダ自身はリネーム禁止
	if (sourcePath.empty() || sourcePath == rootPath || !std::filesystem::exists(sourcePath) ||
		!std::filesystem::is_directory(sourcePath)) {
		result.message = "改名元のフォルダーが見つからないか変更できません";
		return result;
	}

	// フォルダ名にも使えない文字は除去する、空になったら元の名前を維持する
	std::string baseName = ProjectAssetPath::SanitizeFileName(
		requestedName.empty() ? Algorithm::PathToUTF8(sourcePath.filename()) : requestedName);
	if (baseName.empty()) {
		baseName = Algorithm::PathToUTF8(sourcePath.filename());
	}

	const std::filesystem::path targetPath = sourcePath.parent_path() / Algorithm::PathFromUTF8(baseName);
	// 変更がないなら成功扱い
	if (targetPath == sourcePath) {
		result.success = true;
		result.fullPath = sourcePath;
		result.assetPath = directoryVirtualPath;
		return result;
	}
	if (std::filesystem::exists(targetPath)) {
		result.message = "変更先のフォルダーが既に存在します";
		return result;
	}

	// フォルダー内のGUIDを維持して名前だけを変更する
	ProjectAssetMoveTransaction transaction;
	if (!transaction.Add(sourcePath, targetPath)) {
		result.message = "フォルダーの改名計画を作成できません";
		return result;
	}
	result.fullPath = targetPath;
	result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
	if (!transaction.Execute(result.message)) {
		return result;
	}
	transaction.Commit();
	result.success = true;
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetMoveUtility::MoveAsset(
	const ProjectAssetEntry& asset, ProjectAssetSource targetSource, const std::string& targetDirectoryVirtualPath) {

	ProjectAssetFileResult result{};

	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	const std::filesystem::path targetDirectory =
		ProjectAssetPath::ResolveVirtualDirectory(targetSource, targetDirectoryVirtualPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath) || targetDirectory.empty()) {
		result.message = "移動元のAssetまたは移動先のフォルダーが見つかりません";
		return result;
	}

	// 移動先ディレクトリの確保
	std::error_code ec;
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "移動先のフォルダーを作成できません";
		return result;
	}

	// 移動後のパスを重複回避で決定する
	const std::filesystem::path targetPath = ProjectAssetPath::MakeUniquePath(targetDirectory / sourcePath.filename());
	if (targetPath.empty()) {
		result.message = "移動先のパスを決定できません";
		return result;
	}

	// 移動開始前に本体と付随ファイルの計画を確定する
	ProjectAssetMoveTransaction transaction;
	if (!transaction.Add(sourcePath, targetPath)) {
		result.message = "Assetの移動計画を作成できません";
		return result;
	}

	// サイドカーファイルの移動でメタファイルの名前変更も含む
	for (const std::filesystem::path& sidecarSource : ProjectAssetDocumentPatch::BuildAssetSidecarPaths(asset, sourcePath)) {
		if (!std::filesystem::exists(sidecarSource)) {
			continue;
		}

		const std::filesystem::path sidecarTarget = sidecarSource == ProjectAssetPath::MakeMetaPath(sourcePath)
														? ProjectAssetPath::MakeMetaPath(targetPath)
														: targetPath.parent_path() / sidecarSource.filename();

		if (!transaction.Add(sidecarSource, sidecarTarget)) {
			result.message = "付随ファイルの移動先が重複しています";
			return result;
		}
	}
	// 新しい配置先から元の共有ファイルを参照する
	if (ModelDocumentReferences::IsDocumentPath(sourcePath) &&
		!transaction.Prepare(
			0,
			[&](std::string& bytes) { return ModelDocumentReferences::Rebase(sourcePath, targetPath, bytes, result.message); },
			result.message)) {
		return result;
	}
	// 結果の文字列も移動前に確保する
	result.fullPath = targetPath;
	result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
	if (!transaction.Execute(result.message)) {
		if (!transaction.Rollback()) {
			result.message += " 元のファイルへ戻せません。ログを確認してください";
		}
		return result;
	}

	result.success = true;
	transaction.Commit();
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetMoveUtility::MoveDirectory(
	ProjectAssetSource source, const std::string& sourceDirectoryVirtualPath, const std::string& targetDirectoryVirtualPath) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ProjectAssetPath::ResolveVirtualDirectory(source, sourceDirectoryVirtualPath);
	const std::filesystem::path targetDirectory = ProjectAssetPath::ResolveVirtualDirectory(source, targetDirectoryVirtualPath);
	const std::filesystem::path rootPath = ProjectAssetPath::GetSourceRoot(source);

	// 移動元と移動先の検証でルートフォルダの移動や自分自身への移動は禁止
	if (sourcePath.empty() || targetDirectory.empty() || sourcePath == rootPath || !std::filesystem::exists(sourcePath) ||
		!std::filesystem::is_directory(sourcePath)) {
		result.message = "移動元または移動先のフォルダーが見つかりません";
		return result;
	}
	if (ProjectAssetPath::IsSameOrChildPath(targetDirectory, sourcePath)) {
		result.message = "フォルダーを自身の内部へ移動できません";
		return result;
	}

	std::error_code ec;
	std::filesystem::create_directories(targetDirectory, ec);
	if (ec) {
		result.message = "移動先のフォルダーを作成できません";
		return result;
	}

	const std::filesystem::path targetPath = ProjectAssetPath::MakeUniquePath(targetDirectory / sourcePath.filename());
	if (targetPath.empty()) {
		result.message = "移動先のパスを決定できません";
		return result;
	}

	// ディレクトリ全体の移動
	ProjectAssetMoveTransaction transaction;
	if (!transaction.Add(sourcePath, targetPath)) {
		result.message = "フォルダーの移動計画を作成できません";
		return result;
	}
	result.fullPath = targetPath;
	result.assetPath = ProjectAssetPath::ToAssetPath(targetPath);
	if (!transaction.Execute(result.message)) {
		return result;
	}
	transaction.Commit();
	result.success = true;
	return result;
}
