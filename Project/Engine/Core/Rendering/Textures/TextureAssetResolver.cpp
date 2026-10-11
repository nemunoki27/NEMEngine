#include "TextureAssetResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Import/GLTFDocumentReferences.h>
#include <Engine/Core/Rendering/Meshes/Import/GLTFFileReference.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <array>
#include <algorithm>
#include <limits>

//============================================================================
//	TextureAssetResolver classMethods
//============================================================================
std::string Engine::TextureAssetResolver::NormalizeStem(std::string_view name) {

	const std::filesystem::path path = Algorithm::PathFromUTF8(std::string(name));
	return Algorithm::ToLower(Algorithm::PathToUTF8(path.stem()));
}

bool Engine::TextureAssetResolver::IsTextureExtension(const std::filesystem::path& path) {

	const std::string ext = Algorithm::ToLower(Algorithm::PathToUTF8(path.extension()));
	return ext == ".dds" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" ||
		   ext == ".gif" || ext == ".hdr";
}

std::string Engine::TextureAssetResolver::ToAssetPath(const std::filesystem::path& fullPath) {

	return RuntimePaths::ToAssetPath(fullPath);
}

void Engine::TextureAssetResolver::IndexDirectoryRecursive(const std::filesystem::path& directory, bool inPreferredFolder) {

	if (directory.empty() || !std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
		return;
	}

	for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {

		if (!entry.is_regular_file()) {
			continue;
		}

		const std::filesystem::path fullPath = entry.path();
		if (!IsTextureExtension(fullPath)) {
			continue;
		}

		TextureCandidate candidate{};
		candidate.fullPath = fullPath.lexically_normal();
		candidate.assetPath = ToAssetPath(candidate.fullPath);
		candidate.stemLower = Algorithm::ToLower(Algorithm::PathToUTF8(candidate.fullPath.stem()));
		candidate.extLower = Algorithm::ToLower(candidate.fullPath.extension().string());
		candidate.inPreferredFolder = inPreferredFolder;

		if (candidate.stemLower.empty()) {
			continue;
		}

		candidatesByStem_[candidate.stemLower].push_back(std::move(candidate));
	}
}

const Engine::TextureAssetResolver::TextureCandidate* Engine::TextureAssetResolver::ChooseBestCandidate(
	const std::vector<TextureCandidate>& candidates, bool filePaths) const {

	if (candidates.empty()) {
		return nullptr;
	}

	const TextureCandidate* best = nullptr;
	int bestScore = (std::numeric_limits<int>::min)();
	std::string bestExternalKey;
	std::string_view bestKey;
	for (const TextureCandidate& candidate : candidates) {

		// 通常描画ではAssetにできない候補を選ばない
		if (!filePaths && candidate.assetPath.empty()) {
			continue;
		}
		int score = 0;

		// モデル周辺と専用フォルダーを優先する
		if (candidate.inPreferredFolder) {
			score += 1000;
		}
		if (candidate.extLower == ".dds") {
			score += 100;
		}

		// パスが短いものを少し優先
		const auto externalKey = candidate.assetPath.empty() ? Algorithm::PathToUTF8(candidate.fullPath) : std::string{};
		const std::string_view key = candidate.assetPath.empty() ? externalKey : candidate.assetPath;
		score -= static_cast<int>(key.size());
		// 同順位の候補は列挙順に依存させない
		if (best == nullptr || bestScore < score || (bestScore == score && key < bestKey)) {
			best = &candidate;
			bestScore = score;
			// Assetの候補名はコピーせず読み取る
			if (candidate.assetPath.empty()) {
				bestExternalKey = externalKey;
				bestKey = bestExternalKey;
			} else {
				bestKey = candidate.assetPath;
			}
		}
	}
	return best;
}

const Engine::TextureAssetResolver::TextureCandidate* Engine::TextureAssetResolver::ResolveIndexedPathByStem(
	const std::string& stemLower, bool filePaths) const {

	if (stemLower.empty()) {
		return {};
	}

	auto it = candidatesByStem_.find(stemLower);
	if (it == candidatesByStem_.end()) {
		return {};
	}

	return ChooseBestCandidate(it->second, filePaths);
}

void Engine::TextureAssetResolver::Build(const std::filesystem::path& modelFullPath) {

	candidatesByStem_.clear();
	modelDirectory_.clear();
	preferredFolder_.clear();
	texturesRoot_ = RuntimePaths::GetEngineAssetPath("Textures");
	modelDirectory_ = modelFullPath.parent_path();
	uriReferences_ = GLTFDocumentReferences::IsDocumentPath(modelFullPath);

	// モデル周辺の相対参照を優先する
	if (std::filesystem::exists(modelDirectory_) && std::filesystem::is_directory(modelDirectory_)) {

		IndexDirectoryRecursive(modelDirectory_, true);
	}

	const std::filesystem::path gameTexturesRoot = RuntimePaths::GetGameRoot() / "GameAssets/Textures";
	if (std::filesystem::exists(gameTexturesRoot) && std::filesystem::is_directory(gameTexturesRoot)) {

		IndexDirectoryRecursive(gameTexturesRoot, false);
	}

	if (!std::filesystem::exists(texturesRoot_) || !std::filesystem::is_directory(texturesRoot_)) {
		return;
	}

	// モデルファイルと同名のサブフォルダを優先的に検索
	preferredFolder_ = texturesRoot_ / modelFullPath.stem();
	if (std::filesystem::exists(preferredFolder_) && std::filesystem::is_directory(preferredFolder_)) {

		IndexDirectoryRecursive(preferredFolder_, true);
	}
	IndexDirectoryRecursive(texturesRoot_, false);
}

std::string Engine::TextureAssetResolver::ResolveAssetPath(const std::string& importedReference) const {

	return ToAssetPath(ResolvePath(importedReference, false));
}

std::filesystem::path Engine::TextureAssetResolver::ResolveFilePath(const std::string& importedReference) const {

	return ResolvePath(importedReference, true);
}

std::filesystem::path Engine::TextureAssetResolver::ResolvePath(const std::string& importedReference, bool filePaths) const {

	if (importedReference.empty()) {
		return {};
	}
	if (importedReference[0] == '*') {
		return {};
	}

	auto tryDirectPath = [&](const std::filesystem::path& candidate) -> std::filesystem::path {
		if (candidate.empty() || !IsTextureExtension(candidate)) {
			return {};
		}

		std::error_code ec;
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, ec);
		const std::filesystem::path fullPath = ec ? candidate.lexically_normal() : canonical;
		if (!std::filesystem::exists(fullPath) || !std::filesystem::is_regular_file(fullPath)) {
			return {};
		}
		if (!filePaths && ToAssetPath(fullPath).empty()) {
			return {};
		}
		return fullPath;
	};

	const std::filesystem::path referencePath =
		uriReferences_ ? GLTFFileReference::Decode(importedReference) : Algorithm::PathFromUTF8(importedReference);
	if (referencePath.is_absolute()) {
		if (auto direct = tryDirectPath(referencePath); !direct.empty()) {
			return direct;
		}
	} else if (!modelDirectory_.empty()) {
		if (auto direct = tryDirectPath(modelDirectory_ / referencePath); !direct.empty()) {
			return direct;
		}
		if (auto direct = tryDirectPath(modelDirectory_ / referencePath.filename()); !direct.empty()) {
			return direct;
		}
	}

	const std::string stemLower = NormalizeStem(Algorithm::PathToUTF8(referencePath));
	if (stemLower.empty()) {
		return {};
	}
	const auto* candidate = ResolveIndexedPathByStem(stemLower, filePaths);
	return candidate ? candidate->fullPath : std::filesystem::path{};
}

std::string Engine::TextureAssetResolver::ResolveNormalAssetPath(
	const std::string& importedNormalReference, const std::string& importedBaseColorReference) const {

	return ToAssetPath(ResolveNormalPath(importedNormalReference, importedBaseColorReference, false));
}

std::filesystem::path Engine::TextureAssetResolver::ResolveNormalFilePath(
	const std::string& importedNormalReference, const std::string& importedBaseColorReference) const {

	return ResolveNormalPath(importedNormalReference, importedBaseColorReference, true);
}

std::filesystem::path Engine::TextureAssetResolver::ResolveNormalPath(
	const std::string& importedNormalReference, const std::string& importedBaseColorReference, bool filePaths) const {

	if (auto explicitNormal = ResolvePath(importedNormalReference, filePaths); !explicitNormal.empty()) {
		return explicitNormal;
	}

	const auto resolvedBaseColor = ResolvePath(importedBaseColorReference, filePaths);
	const auto basePath = resolvedBaseColor.empty()
		? (uriReferences_ ? GLTFFileReference::Decode(importedBaseColorReference)
						  : Algorithm::PathFromUTF8(importedBaseColorReference)) : resolvedBaseColor;
	std::string baseStem = NormalizeStem(Algorithm::PathToUTF8(basePath));
	if (baseStem.empty()) {
		return {};
	}

	auto pushUnique = [](std::vector<std::string>& values, std::string value) {
		if (!value.empty() && std::find(values.begin(), values.end(), value) == values.end()) {
			values.emplace_back(std::move(value));
		}
	};

	std::vector<std::string> baseStems{};
	baseStems.reserve(2);
	pushUnique(baseStems, baseStem);

	constexpr std::array colorSuffixes = {"_base_color", "_basecolor", "_diffuse", "_albedo", "_color", "_diff", "_dif",
		"-base-color", "-basecolor", "-diffuse", "-albedo", "-color", "-diff", "-dif"};
	for (const std::string_view suffix : colorSuffixes) {

		if (baseStem.size() <= suffix.size()) {
			continue;
		}
		if (!Algorithm::EndsWith(baseStem, std::string(suffix))) {
			continue;
		}

		pushUnique(baseStems, baseStem.substr(0, baseStem.size() - suffix.size()));
		break;
	}

	constexpr std::array normalSuffixes = {
		"_ddn", "_normal", "_norm", "_nrm", "_bump", "-ddn", "-normal", "-norm", "-nrm", "-bump"};
	for (const std::string& stem : baseStems) {
		for (const std::string_view suffix : normalSuffixes) {

			if (const auto* candidate = ResolveIndexedPathByStem(stem + std::string(suffix), filePaths)) {
				return candidate->fullPath;
			}
		}
	}

	return {};
}
