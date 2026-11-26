#include "ProjectModelImportPlan.h"

//============================================================================
//	include
//============================================================================
#include "ProjectDirectoryCopyTransaction.h"
#include "ProjectAssetCopyTransaction.h"
#include "ProjectAssetPath.h"
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/StorageDirectoryLease.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileDependencyCollector.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelDocumentReferences.h>
#include <Engine/Core/Rendering/Meshes/Import/GLTFFileReference.h>
#include <Engine/Core/Rendering/Meshes/Import/FBXDocumentReferences.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <memory>
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
bool Engine::ProjectModelImportPlan::Prepare(const std::filesystem::path& model, std::string& diagnostic, Layout* layout) {

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
		Layout local{ .sourceRoot = model_.parent_path() };
		prepared_ = PreparePaths(layout ? *layout : local, diagnostic) && ValidateSources(diagnostic);
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
	entry.outputRevision = entry.revision;
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

bool Engine::ProjectModelImportPlan::PreparePaths(Layout& layout, std::string& diagnostic) {

	const auto root = std::filesystem::weakly_canonical(layout.sourceRoot);
	auto main = model_.lexically_relative(root);
	if (!layout.targetRoot.empty()) {
		main = ProjectAssetPath::MakeUniquePath(layout.targetRoot / main).lexically_relative(layout.targetRoot);
	}
	if (main.empty() || !ProjectAssetPath::IsSafeRelativePath(main)) {
		diagnostic = "モデルの保存先が無効です";
		return false;
	}
	files_.front().relative = main;
	const auto prefix = main.parent_path() / Algorithm::PathFromUTF8(Algorithm::PathToUTF8(main.stem()) + "_Dependencies");
	auto uniqueDirectory = [&](const std::filesystem::path& preferred) {
		auto candidate = preferred;
		for (uint32_t index = 1; ; ++index) {
			if (!std::filesystem::exists(root / candidate) &&
				(layout.targetRoot.empty() || !std::filesystem::exists(layout.targetRoot / candidate)) &&
				layout.reserved.insert(StorageFileUtility::PathKey(root / candidate)).second) {
				return candidate;
			}
			candidate = preferred.parent_path() /
				Algorithm::PathFromUTF8(Algorithm::PathToUTF8(preferred.filename()) + " " + std::to_string(index));
		}
	};
	std::filesystem::path dependencyRoot;
	auto dependencies = [&]() -> const std::filesystem::path& {
		if (dependencyRoot.empty()) dependencyRoot = uniqueDirectory(prefix);
		return dependencyRoot;
	};
	for (size_t i = 1; i < files_.size(); ++i) {
		auto& file = files_[i];
		if (StorageFileUtility::IsInside(file.source, root)) {
			file.relative = file.source.lexically_relative(root);
			continue;
		}
		const auto parent = file.source.parent_path();
		auto& directory = layout.externalDirectories[StorageFileUtility::PathKey(parent)];
		if (directory.empty()) directory = uniqueDirectory(dependencies() / parent.filename());
		file.relative = directory / file.source.filename();
	}
	if (!RewriteReferences(diagnostic)) return false;
	if (layout.targetRoot.empty()) return true;

	// 同名の別画像がある場合は補完用Normalも一緒に分ける
	bool conflict = false;
	for (size_t i = 1; i < files_.size() && !conflict; ++i) {
		const auto& file = files_[i];
		if (!StorageFileUtility::IsInside(file.source, root)) continue;
		const auto dest = layout.targetRoot / file.relative;
		if (std::filesystem::is_regular_file(dest)) {
			conflict = StorageFileUtility::FileRevision(dest) != file.outputRevision;
		} else {
			conflict = std::filesystem::exists(dest) || std::filesystem::exists(ProjectAssetPath::MakeMetaPath(dest));
		}
		for (auto parent = dest.parent_path(); !conflict && parent != layout.targetRoot; parent = parent.parent_path()) {
			conflict = std::filesystem::exists(parent) && !std::filesystem::is_directory(parent);
		}
	}
	if (!conflict) return true;
	const auto local = uniqueDirectory(prefix);
	for (auto& file : files_) {
		if (&file != &files_.front() && StorageFileUtility::IsInside(file.source, root)) file.relative = local / file.relative;
		if (&file == &files_.front() || IsMaterial(file.source)) {
			file.bytes = StorageFileUtility::ReadVerifiedBytes(file.source, file.revision);
		}
	}
	return RewriteReferences(diagnostic);
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
		entry.outputRevision = ContentHash::SHA256({reinterpret_cast<const uint8_t*>(entry.bytes.data()), entry.bytes.size()});
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
	std::set<std::string>* staged) const {

	if (!prepared_) {
		diagnostic = "モデルの取り込み計画が準備されていません";
		return false;
	}
	if (!ValidateSources(diagnostic)) {
		return false;
	}
	for (const auto& entry : files_) {
		if (staged && !staged->insert(entry.key).second) continue;
		// コピー元の変更を拒否して準備済みの参照だけを反映する
		const auto prepare = [&](const auto& path, std::string& bytes) {
			if (StorageFileUtility::FileRevision(path) != entry.revision) {
				return false;
			}
			bytes = entry.bytes;
			return true;
		};
		if (!transaction.StageFile(entry.source, entry.relative, diagnostic, prepare)) {
			return false;
		}
	}
	return ValidateSources(diagnostic);
}

bool Engine::ProjectModelImportPlan::PublishFiles(const std::filesystem::path& root, std::string& diagnostic) const {

	if (!prepared_ || !ValidateSources(diagnostic)) return false;
	struct CreatedDirectories {
		std::vector<std::pair<std::filesystem::path, StorageFileUtility::FileIdentity>> owned;
		~CreatedDirectories() {
			std::error_code error;
			for (auto it = owned.rbegin(); it != owned.rend(); ++it)
				StorageFileUtility::RemoveIfIdentity(it->first, it->second, error);
		}
	} directories;
	std::map<std::filesystem::path, std::unique_ptr<StorageDirectoryLease>> parents;
	std::vector<std::unique_ptr<ProjectAssetCopyTransaction>> copies;
	std::vector<const FileEntry*> shared;
	auto acquire = [&](const std::filesystem::path& path, bool create) {
		if (parents.contains(path)) return;
		std::error_code error;
		const bool created = create && std::filesystem::create_directory(path);
		StorageFileUtility::FileIdentity identity{};
		if (!StorageFileUtility::ReadIdentity(path, identity, error)) throw std::system_error(error);
		if (created) directories.owned.emplace_back(path, identity);
		parents[path] = std::make_unique<StorageDirectoryLease>(path, identity);
	};
	acquire(root, false);
	for (const auto& entry : files_) {
		auto parent = root;
		for (const auto& part : entry.relative.parent_path()) {
			parent /= part;
			acquire(parent, true);
		}
		const auto dest = root / entry.relative;
		if (std::filesystem::is_regular_file(dest) && entry.key != files_.front().key &&
			StorageFileUtility::FileRevision(dest) == entry.outputRevision) {
			shared.push_back(&entry);
			continue;
		}
		auto copy = std::make_unique<ProjectAssetCopyTransaction>(parent);
		if (!copy->Add(entry.source, dest) || !copy->Stage(diagnostic)) return false;
		if (copy->GetStagedSnapshot(0).revision != entry.revision) {
			diagnostic = "取り込み準備中にモデルの参照ファイルが変更されました";
			return false;
		}
		if (entry.outputRevision != entry.revision) {
			const auto prepare = [&](const auto&, std::string& bytes) {
				bytes = entry.bytes;
				return true;
			};
			if (!copy->Prepare(0, prepare, diagnostic)) return false;
		}
		copies.push_back(std::move(copy));
	}
	// 参照先を先に公開し、モデルの読込に成功してから所有を渡す
	for (auto it = copies.rbegin(); it != copies.rend(); ++it) {
		if (!(*it)->Publish(diagnostic)) return false;
	}
	if (!Verify(root, diagnostic)) return false;
	for (const auto* entry : shared) {
		if (StorageFileUtility::FileRevision(root / entry->relative) != entry->outputRevision) {
			diagnostic = "取り込み中に共有する参照ファイルが変更されました";
			return false;
		}
	}
	for (const auto& copy : copies) copy->Commit();
	directories.owned.clear();
	return true;
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
