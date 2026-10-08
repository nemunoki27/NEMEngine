#include "ProjectAssetFileUtility.h"
#include "ProjectAssetCopyUtility.h"
#include "ProjectAssetMoveUtility.h"
#include "ProjectAssetPath.h"
#include "ProjectAssetDocumentFactory.h"
#include "ProjectAssetDocumentPatch.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <exception>
#include <system_error>

//============================================================================
//	ProjectAssetFileUtility classMethods
//============================================================================

const char* Engine::ProjectAssetFileUtility::GetCreateMenuLabel(ProjectAssetFileKind kind) {

	switch (kind) {
	case ProjectAssetFileKind::Folder:
		return "Folder";
	case ProjectAssetFileKind::Text:
		return "Text File";
	case ProjectAssetFileKind::Script:
		return "C# Script";
	case ProjectAssetFileKind::Scene:
		return "Scene";
	case ProjectAssetFileKind::Prefab:
		return "Prefab";
	case ProjectAssetFileKind::Material:
		return "Material";
	case ProjectAssetFileKind::AnimationClip:
		return "AnimationClip";
	case ProjectAssetFileKind::AnimationController:
		return "Animation Controller";
	case ProjectAssetFileKind::Shader:
		return "Shader";
	case ProjectAssetFileKind::RenderPipeline:
		return "Render Pipeline";
	case ProjectAssetFileKind::ShaderGraph:
		return "Shader Graph";
	case ProjectAssetFileKind::RenderPasses:
		return "Render Passes";
	case ProjectAssetFileKind::RenderTexture:
		return "Render Texture";
	}
	return "Asset";
}

const char* Engine::ProjectAssetFileUtility::GetDefaultName(ProjectAssetFileKind kind) {

	switch (kind) {
	case ProjectAssetFileKind::Folder:
		return "New Folder";
	case ProjectAssetFileKind::Text:
		return "New Text";
	case ProjectAssetFileKind::Script:
		return "NewScript";
	case ProjectAssetFileKind::Scene:
		return "NewScene";
	case ProjectAssetFileKind::Prefab:
		return "NewPrefab";
	case ProjectAssetFileKind::Material:
		return "NewMaterial";
	case ProjectAssetFileKind::AnimationClip:
		return "NewAnimation";
	case ProjectAssetFileKind::AnimationController:
		return "NewAnimationController";
	case ProjectAssetFileKind::Shader:
		return "NewShader";
	case ProjectAssetFileKind::RenderPipeline:
		return "NewPipeline";
	case ProjectAssetFileKind::ShaderGraph:
		return "NewShaderGraph";
	case ProjectAssetFileKind::RenderPasses:
		return "NewRenderPasses";
	case ProjectAssetFileKind::RenderTexture:
		return "NewRenderTexture";
	}
	return "NewAsset";
}

std::string Engine::ProjectAssetFileUtility::GetEditableAssetName(const ProjectAssetEntry& asset) {
	// 拡張子を取り除いた、編集可能な名前部分を抽出
	return ProjectAssetPath::SplitAssetFileName(Algorithm::PathFromUTF8(asset.assetPath)).first;
}

std::string Engine::ProjectAssetFileUtility::GetProtectedAssetSuffix(const ProjectAssetEntry& asset) {
	// 変更不可能なアセット固有の拡張子部分を抽出
	return ProjectAssetPath::SplitAssetFileName(Algorithm::PathFromUTF8(asset.assetPath)).second;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::PlanCreate(ProjectAssetSource source,
	const std::string& directoryVirtualPath, ProjectAssetFileKind kind, const std::string& requestedName) {

	ProjectAssetFileResult result{};
	result.isDirectory = kind == ProjectAssetFileKind::Folder;
	try {

		// 仮想パスから実ディレクトリを特定し失敗なら中断
		const std::filesystem::path directory = ProjectAssetPath::ResolveVirtualDirectory(source, directoryVirtualPath);
		if (directory.empty()) {
			result.message = "作成先のフォルダーを解決できません";
			return result;
		}
		if (std::filesystem::exists(directory) && !std::filesystem::is_directory(directory)) {
			result.message = "作成先がフォルダーではありません";
			return result;
		}

		// 拡張子の決定とファイル名のサニタイズ
		const char* suffix = ProjectAssetDocumentFactory::GetFileSuffix(kind);
		std::string baseName = ProjectAssetPath::SanitizeFileName(requestedName.empty() ? GetDefaultName(kind) : requestedName);
		if (kind != ProjectAssetFileKind::Folder) {
			baseName = ProjectAssetPath::RemoveTypedSuffix(baseName, suffix);
		}

		// 同一名称がある場合は自動的に連番を付与して一意のパスを作成
		const std::string requestedPath = kind == ProjectAssetFileKind::Folder ? baseName : baseName + suffix;
		const std::filesystem::path preferredPath = directory / Algorithm::PathFromUTF8(requestedPath);
		const std::filesystem::path createPath = ProjectAssetPath::MakeUniquePath(preferredPath);
		if (createPath.empty()) {
			result.message = "Assetの作成先を決定できません";
			return result;
		}

		// 公開後に確保が必要な結果は先に準備する
		result.fullPath = createPath;
		result.assetPath = ProjectAssetPath::ToAssetPath(createPath);
		result.success = true;
		return result;
	} catch (const std::exception& error) {
		// 作成先の確認に失敗しても既存ファイルを残す
		result.message = "Assetの作成先を確認できません: " + std::string(error.what());
		return result;
	}
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::Create(ProjectAssetSource source,
	const std::string& directoryVirtualPath, ProjectAssetFileKind kind, const std::string& requestedName) {

	auto result = PlanCreate(source, directoryVirtualPath, kind, requestedName);
	if (!result.success) {
		return result;
	}
	result.success = false;
	// 公開先の親を用意してから本体を作る
	std::error_code ec;
	std::filesystem::create_directories(result.fullPath.parent_path(), ec);
	if (ec) {
		result.message = "作成先のフォルダーを用意できません";
		return result;
	}
	// 既存のファイルとフォルダーを所有しない
	if (kind == ProjectAssetFileKind::Folder) {
		if (!std::filesystem::create_directory(result.fullPath, ec) || ec) {
			result.message = "フォルダーを作成できません";
			return result;
		}
	} else {
		// 確定したファイル名で文書とScriptの型名を作る
		if (!ProjectAssetDocumentFactory::CreateTextFile(
				result.fullPath, ProjectAssetDocumentFactory::BuildFileContent(
									 kind, ProjectAssetPath::SplitAssetFileName(result.fullPath).first))) {
			result.message = "Assetの文書を作成できません";
			return result;
		}
	}

	result.success = true;
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DuplicateAsset(
	const ProjectAssetEntry& asset, const std::shared_ptr<SceneAssetStorage>& storage) {

	return ProjectAssetCopyUtility::DuplicateAsset(asset, storage);
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::CopyAsset(const ProjectAssetEntry& asset,
	ProjectAssetSource targetSource, const std::string& targetDirectoryVirtualPath,
	const std::shared_ptr<SceneAssetStorage>& storage) {

	return ProjectAssetCopyUtility::CopyAsset(asset, targetSource, targetDirectoryVirtualPath, storage);
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::RenameAsset(
	const ProjectAssetEntry& asset, const std::string& requestedName) {

	return ProjectAssetMoveUtility::RenameAsset(asset, requestedName);
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::RenameDirectory(
	ProjectAssetSource source, const std::string& directoryVirtualPath, const std::string& requestedName) {

	return ProjectAssetMoveUtility::RenameDirectory(source, directoryVirtualPath, requestedName);
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DuplicateDirectory(
	ProjectAssetSource source, const std::string& directoryVirtualPath, const std::shared_ptr<SceneAssetStorage>& storage) {

	return ProjectAssetCopyUtility::DuplicateDirectory(source, directoryVirtualPath, storage);
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DeleteAsset(
	const ProjectAssetEntry& asset, const AssetDatabase& database, const std::shared_ptr<SceneAssetStorage>& storage) {

	ProjectAssetFileResult result{};

	const std::filesystem::path sourcePath = RuntimePaths::ResolveAssetPath(asset.assetPath);
	if (sourcePath.empty() || !std::filesystem::exists(sourcePath)) {
		result.message = "Source asset was not found.";
		return result;
	}

	// 本体と付随ファイルを退避し、途中失敗時はまとめて戻す
	const auto sidecars = ProjectAssetDocumentPatch::BuildAssetSidecarPaths(asset, sourcePath);
	result.success = storage->Delete(sourcePath, database, result.message, sidecars);
	result.assetPath = asset.assetPath;
	result.fullPath = sourcePath;
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::DeleteDirectory(ProjectAssetSource source,
	const std::string& directoryVirtualPath, const AssetDatabase& database, const std::shared_ptr<SceneAssetStorage>& storage) {

	ProjectAssetFileResult result{};
	result.isDirectory = true;

	const std::filesystem::path sourcePath = ProjectAssetPath::ResolveVirtualDirectory(source, directoryVirtualPath);
	const std::filesystem::path rootPath = ProjectAssetPath::GetSourceRoot(source);
	// ルートフォルダ自身は削除禁止
	if (sourcePath.empty() || sourcePath == rootPath || !std::filesystem::exists(sourcePath) ||
		!std::filesystem::is_directory(sourcePath)) {
		result.message = "Source folder was not found or cannot be deleted.";
		return result;
	}

	// フォルダー外にある所有Actorもまとめて退避する
	if (!storage->Delete(sourcePath, database, result.message)) {
		return result;
	}

	result.success = true;
	result.assetPath = ProjectAssetPath::ToAssetPath(sourcePath.parent_path());
	result.fullPath = sourcePath;
	return result;
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::MoveAsset(
	const ProjectAssetEntry& asset, ProjectAssetSource targetSource, const std::string& targetDirectoryVirtualPath) {

	return ProjectAssetMoveUtility::MoveAsset(asset, targetSource, targetDirectoryVirtualPath);
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::MoveDirectory(
	ProjectAssetSource source, const std::string& sourceDirectoryVirtualPath, const std::string& targetDirectoryVirtualPath) {

	return ProjectAssetMoveUtility::MoveDirectory(source, sourceDirectoryVirtualPath, targetDirectoryVirtualPath);
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::ImportExternalFile(ProjectAssetSource targetSource,
	const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalFilePath) {

	return ProjectAssetCopyUtility::ImportExternalFile(targetSource, targetDirectoryVirtualPath, externalFilePath);
}

Engine::ProjectAssetFileResult Engine::ProjectAssetFileUtility::ImportExternalDirectory(ProjectAssetSource targetSource,
	const std::string& targetDirectoryVirtualPath, const std::filesystem::path& externalDirectoryPath) {

	return ProjectAssetCopyUtility::ImportExternalDirectory(targetSource, targetDirectoryVirtualPath, externalDirectoryPath);
}

namespace Engine {
	std::filesystem::path ProjectAssetFileUtility::GetSourceRoot(ProjectAssetSource source) {

		return ProjectAssetPath::GetSourceRoot(source);
	}
}

namespace Engine {
	std::filesystem::path ProjectAssetFileUtility::ResolveVirtualDirectory(
		ProjectAssetSource source, const std::string& directoryVirtualPath) {

		return ProjectAssetPath::ResolveVirtualDirectory(source, directoryVirtualPath);
	}
}
