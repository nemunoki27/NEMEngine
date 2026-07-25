#include "TextureAssetResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <array>

//============================================================================
//	TextureAssetResolver classMethods
//============================================================================
std::string Engine::TextureAssetResolver::NormalizeStem(const std::string_view& name) {

	const std::filesystem::path path = Algorithm::PathFromUTF8(std::string(name));
	return Algorithm::ToLower(Algorithm::PathToUTF8(path.stem()));
}

bool Engine::TextureAssetResolver::IsTextureExtension(const std::filesystem::path& path) {

	const std::string ext = Algorithm::ToLower(path.extension().string());
	return ext == ".dds" || ext == ".png" || ext == ".jpg" ||
		ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
}

std::string Engine::TextureAssetResolver::ToAssetPath(const std::filesystem::path& fullPath) {

	return RuntimePaths::ToAssetPath(fullPath);
}

void Engine::TextureAssetResolver::IndexDirectoryRecursive(
	const std::filesystem::path& directory, bool inPreferredFolder) {

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

		if (candidate.assetPath.empty() || candidate.stemLower.empty()) {
			continue;
		}

		candidatesByStem_[candidate.stemLower].push_back(std::move(candidate));
	}
}

const Engine::TextureAssetResolver::TextureCandidate* Engine::TextureAssetResolver::
ChooseBestCandidate(const std::vector<TextureCandidate>& candidates) const {

	if (candidates.empty()) {
		return nullptr;
	}

	const TextureCandidate* best = nullptr;
	int bestScore = (std::numeric_limits<int>::min)();
	for (const TextureCandidate& candidate : candidates) {

		int score = 0;

		// 優先順位:
		// 1. Engine/Assets/Textures/<modelStem>/...
		// 2. .dds
		// 3.それ以外
		if (candidate.inPreferredFolder) {
			score += 1000;
		}
		if (candidate.extLower == ".dds") {
			score += 100;
		}

		// パスが短いものを少し優先
		score -= static_cast<int>(candidate.assetPath.size());
		if (best == nullptr || bestScore < score) {
			best = &candidate;
			bestScore = score;
		}
	}
	return best;
}

std::string Engine::TextureAssetResolver::ResolveIndexedAssetPathByStem(const std::string& stemLower) const {

	if (stemLower.empty()) {
		return {};
	}

	auto it = candidatesByStem_.find(stemLower);
	if (it == candidatesByStem_.end()) {
		return {};
	}

	const TextureCandidate* best = ChooseBestCandidate(it->second);
	if (!best) {
		return {};
	}
	return best->assetPath;
}

void Engine::TextureAssetResolver::Build(const std::filesystem::path& modelFullPath) {

	candidatesByStem_.clear();
	modelDirectory_.clear();
	preferredFolder_.clear();
	texturesRoot_ = RuntimePaths::GetEngineAssetPath("Textures");
	modelDirectory_ = modelFullPath.parent_path();

	// OBJ/MTLやglTFはモデル横の相対パスを持つことが多いので、モデル周辺を最優先で索引化する
	if (std::filesystem::exists(modelDirectory_) && std::filesystem::is_directory(modelDirectory_)) {

		IndexDirectoryRecursive(modelDirectory_, true);
		const std::filesystem::path localTextures = modelDirectory_ / "Textures";
		if (std::filesystem::exists(localTextures) && std::filesystem::is_directory(localTextures)) {
			IndexDirectoryRecursive(localTextures, true);
		}
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

	if (importedReference.empty()) {
		return {};
	}
	if (importedReference[0] == '*') {
		return {};
	}

	auto tryDirectPath = [&](const std::filesystem::path& candidate) -> std::string {

		if (candidate.empty() || !IsTextureExtension(candidate)) {
			return {};
		}

		std::error_code ec;
		const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, ec);
		const std::filesystem::path fullPath = ec ? candidate.lexically_normal() : canonical;
		if (!std::filesystem::exists(fullPath) || !std::filesystem::is_regular_file(fullPath)) {
			return {};
		}
		return ToAssetPath(fullPath);
		};

	const std::filesystem::path referencePath = Algorithm::PathFromUTF8(importedReference);
	if (referencePath.is_absolute()) {
		if (std::string direct = tryDirectPath(referencePath); !direct.empty()) {
			return direct;
		}
	} else if (!modelDirectory_.empty()) {
		if (std::string direct = tryDirectPath(modelDirectory_ / referencePath); !direct.empty()) {
			return direct;
		}
		if (std::string direct = tryDirectPath(modelDirectory_ / referencePath.filename()); !direct.empty()) {
			return direct;
		}
	}

	const std::string stemLower = NormalizeStem(importedReference);
	if (stemLower.empty()) {
		return {};
	}
	return ResolveIndexedAssetPathByStem(stemLower);
}

std::string Engine::TextureAssetResolver::ResolveNormalAssetPath(
	const std::string& importedNormalReference, const std::string& importedBaseColorReference) const {

	if (std::string explicitNormal = ResolveAssetPath(importedNormalReference); !explicitNormal.empty()) {
		return explicitNormal;
	}

	const std::string resolvedBaseColor = ResolveAssetPath(importedBaseColorReference);
	std::string baseStem = NormalizeStem(resolvedBaseColor.empty() ? importedBaseColorReference : resolvedBaseColor);
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

	constexpr std::array colorSuffixes = {
		"_base_color", "_basecolor", "_diffuse", "_albedo", "_color", "_diff", "_dif",
		"-base-color", "-basecolor", "-diffuse", "-albedo", "-color", "-diff", "-dif"
	};
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
		"_ddn", "_normal", "_norm", "_nrm", "_bump",
		"-ddn", "-normal", "-norm", "-nrm", "-bump"
	};
	for (const std::string& stem : baseStems) {
		for (const std::string_view suffix : normalSuffixes) {

			std::string inferredPath = ResolveIndexedAssetPathByStem(stem + std::string(suffix));
			if (!inferredPath.empty()) {
				return inferredPath;
			}
		}
	}

	return {};
}
