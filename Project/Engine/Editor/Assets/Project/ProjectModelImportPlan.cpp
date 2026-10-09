#include "ProjectModelImportPlan.h"

//============================================================================
//	include
//============================================================================
#include "ProjectDirectoryCopyTransaction.h"
#include "ProjectAssetPath.h"
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>
#include <Engine/Core/Rendering/Meshes/Import/GLTFFileReference.h>
#include <Engine/Core/Rendering/Meshes/Import/FBXDocumentReferences.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <map>
#include <stdexcept>

namespace {

	// MTL文書の拡張子を判定する
	bool IsMaterial(const std::filesystem::path& path) {

		return Engine::Algorithm::ToLower(Engine::Algorithm::PathToUTF8(path.extension())) == ".mtl";
	}
}

//============================================================================
//	ProjectModelImportPlan classMethods
//============================================================================
bool Engine::ProjectModelImportPlan::Prepare(const std::filesystem::path& model, std::string& diagnostic) {

	prepared_ = false;
	files_.clear();
	materialTextures_.clear();
	diagnostic.clear();
	try {
		model_ = std::filesystem::weakly_canonical(model);
		if (!ModelDocumentReferences::IsDocumentPath(model_) || !AddFile(model_, diagnostic)) {
			return false;
		}
		// 通常描画が補完したMTLとNormalも取り込む
		ModelFileDependencyCollector::ModelDependencies dependencies;
		if (!ModelFileDependencyCollector::Collect(model_, dependencies, diagnostic)) {
			return false;
		}
		textures_.Build(model_);
		for (const auto& path : dependencies.files) {
			if (!AddFile(path, diagnostic)) {
				return false;
			}
		}
		materialTextures_ = std::move(dependencies.materialTextures);
		if (!CollectReferences(diagnostic)) {
			return false;
		}
		AssignPaths();
		prepared_ = RewriteReferences(diagnostic) && ValidateSources(diagnostic);
		return prepared_;
	} catch (const std::exception& error) {
		diagnostic = "モデルの取り込み計画を作成できません: " + std::string(error.what());
		return false;
	}
}

bool Engine::ProjectModelImportPlan::AddFile(const std::filesystem::path& path, std::string& diagnostic) {

	const auto source = std::filesystem::weakly_canonical(path);
	if (FindFile(source)) {
		return true;
	}
	if (!std::filesystem::is_regular_file(source)) {
		diagnostic = "モデルの参照ファイルが見つかりません: " + Algorithm::PathToUTF8(source);
		return false;
	}
	FileEntry entry;
	entry.source = source;
	entry.key = StorageFileUtility::PathKey(source);
	entry.revision = StorageFileUtility::FileRevision(source);
	entry.bytes = StorageFileUtility::ReadVerifiedBytes(source, entry.revision);
	files_.push_back(std::move(entry));
	return true;
}

const Engine::ProjectModelImportPlan::FileEntry* Engine::ProjectModelImportPlan::FindFile(
	const std::filesystem::path& path) const {

	const auto key = StorageFileUtility::PathKey(path);
	const auto found = std::ranges::find_if(files_, [&](const auto& entry) { return entry.key == key; });
	return found == files_.end() ? nullptr : &*found;
}

std::filesystem::path Engine::ProjectModelImportPlan::ResolveReference(
	const std::filesystem::path& document, const std::string& reference) const {

	const auto path = GLTFDocumentReferences::IsDocumentPath(document)
		? GLTFFileReference::Decode(reference) : Algorithm::PathFromUTF8(reference);
	const auto resolved = (document.parent_path() / path).lexically_normal();
	if (FBXDocumentReferences::IsDocumentPath(document)) {
		// FBXの絶対参照と画像名補完を通常描画へ合わせる
		return textures_.ResolveFilePath(reference);
	}
	if (IsMaterial(document)) {
		// MTLの位置を含む参照を通常の画像補完へ渡す
		return textures_.ResolveFilePath(Algorithm::PathToUTF8(resolved));
	}
	if (GLTFDocumentReferences::IsDocumentPath(document) || std::filesystem::is_regular_file(resolved)) {
		return resolved;
	}
	// OBJの同名MTL補完を取り込み後は明示参照にする
	auto fallback = model_;
	fallback.replace_extension(".mtl");
	return std::filesystem::is_regular_file(fallback) ? fallback : resolved;
}

bool Engine::ProjectModelImportPlan::CollectReferences(std::string& diagnostic) {

	for (size_t index = 0; index < files_.size(); ++index) {
		// 参照追加で一覧が増えるため解析対象を値で保持する
		const auto source = files_[index].source;
		auto bytes = files_[index].bytes;
		const bool material = IsMaterial(source);
		if (!material && index != 0) {
			continue;
		}
		const auto collect = [&](std::string& reference) {
			const auto path = ResolveReference(source, reference);
			if (path.empty()) {
				diagnostic = "モデルの画像参照を解決できません: " + reference;
				return false;
			}
			return AddFile(path, diagnostic);
		};
		if (!(material ? ModelDocumentReferences::RewriteMaterial(bytes, collect, diagnostic)
					   : ModelDocumentReferences::Rewrite(source, bytes, collect, diagnostic))) {
			return false;
		}
	}
	return true;
}

void Engine::ProjectModelImportPlan::AssignPaths() {

	// 親フォルダー単位で分け、画像名のNormal補完を維持する
	std::map<std::string, std::filesystem::path> directories;
	for (size_t index = 1; index < files_.size(); ++index) {
		directories.emplace(StorageFileUtility::PathKey(files_[index].source.parent_path()), std::filesystem::path{});
	}
	size_t directoryIndex = 0;
	for (auto& [key, path] : directories) {
		path = std::filesystem::path("_resources") / std::to_string(directoryIndex++);
	}
	files_.front().relative = model_.filename();
	for (size_t index = 1; index < files_.size(); ++index) {
		auto& entry = files_[index];
		entry.relative = directories.at(StorageFileUtility::PathKey(entry.source.parent_path())) / entry.source.filename();
	}
}

bool Engine::ProjectModelImportPlan::RewriteReferences(std::string& diagnostic) {

	for (size_t index = 0; index < files_.size(); ++index) {
		auto& entry = files_[index];
		const bool material = IsMaterial(entry.source);
		if (!material && index != 0) {
			continue;
		}
		const auto rewrite = [&](std::string& reference) {
			const auto* target = FindFile(ResolveReference(entry.source, reference));
			if (!target) {
				return false;
			}
			const auto relative = target->relative.lexically_relative(entry.relative.parent_path());
			if (relative.empty()) {
				return false;
			}
			reference = GLTFDocumentReferences::IsDocumentPath(entry.source)
				? GLTFFileReference::Encode(relative) : Algorithm::ConvertString(relative.generic_wstring());
			return true;
		};
		if (!(material ? ModelDocumentReferences::RewriteMaterial(entry.bytes, rewrite, diagnostic)
					   : ModelDocumentReferences::Rewrite(entry.source, entry.bytes, rewrite, diagnostic))) {
			return false;
		}
	}
	return true;
}

bool Engine::ProjectModelImportPlan::ValidateSources(std::string& diagnostic) const {

	for (const auto& entry : files_) {
		if (StorageFileUtility::FileRevision(entry.source) != entry.revision) {
			diagnostic = "取り込み準備中にモデルの参照ファイルが変更されました";
			return false;
		}
	}
	return true;
}

bool Engine::ProjectModelImportPlan::Stage(ProjectDirectoryCopyTransaction& transaction, std::string& diagnostic,
	const std::filesystem::path& relativeRoot) const {

	if (!prepared_) {
		diagnostic = "モデルの取り込み計画が準備されていません";
		return false;
	}
	if (!ValidateSources(diagnostic)) {
		return false;
	}
	if (!ProjectAssetPath::IsSafeRelativePath(relativeRoot)) {
		diagnostic = "モデルの取り込み先の相対位置が無効です";
		return false;
	}
	for (const auto& entry : files_) {
		// コピー元の変更を拒否して準備済みの参照だけを反映する
		const auto prepare = [&](const auto& path, std::string& bytes) {
			if (StorageFileUtility::FileRevision(path) != entry.revision) {
				return false;
			}
			bytes = entry.bytes;
			return true;
		};
		if (!transaction.StageFile(entry.source, relativeRoot / entry.relative, diagnostic, prepare)) {
			return false;
		}
	}
	return ValidateSources(diagnostic);
}

bool Engine::ProjectModelImportPlan::Verify(const std::filesystem::path& root, std::string& diagnostic) const {

	std::vector<std::vector<std::filesystem::path>> textures;
	if (!ModelFileDependencyCollector::CollectMaterialTextures(root / GetMainRelativePath(), textures, diagnostic) ||
		textures.size() != materialTextures_.size()) {
		diagnostic = "取り込み後のMaterial構成を確認できません: " + diagnostic;
		return false;
	}
	// 同名画像の補完が別の画像へ変わった場合も成功扱いにしない
	for (size_t material = 0; material < textures.size(); ++material) {
		if (textures[material].size() != materialTextures_[material].size()) {
			diagnostic = "取り込み後のMaterialの画像参照数が変わりました";
			return false;
		}
		for (size_t index = 0; index < textures[material].size(); ++index) {
			const auto* entry = FindFile(materialTextures_[material][index]);
			if (!entry ||
				StorageFileUtility::PathKey(textures[material][index]) != StorageFileUtility::PathKey(root / entry->relative)) {
				diagnostic = "取り込み後のMaterialが元と異なる画像を参照しています";
				return false;
			}
		}
	}
	return ValidateSources(diagnostic);
}
