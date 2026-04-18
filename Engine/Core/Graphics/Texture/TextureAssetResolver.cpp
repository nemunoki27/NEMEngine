#include "TextureAssetResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Algorithm/Algorithm.h>

//============================================================================
//	TextureAssetResolver classMethods
//============================================================================

std::string Engine::TextureAssetResolver::NormalizeStem(const std::string_view& name) {

	std::filesystem::path path(name);
	return Algorithm::ToLower(path.stem().string());
}

bool Engine::TextureAssetResolver::IsTextureExtension(const std::filesystem::path& path) {

	const std::string ext = Algorithm::ToLower(path.extension().string());
	return ext == ".dds" || ext == ".png" || ext == ".jpg" ||
		ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
}

std::string Engine::TextureAssetResolver::ToAssetPath(const std::filesystem::path& fullPath) {

	std::error_code ec;
	std::filesystem::path relative = std::filesystem::relative(fullPath, std::filesystem::path("."), ec);
	if (ec) {
		return {};
	}

	const std::string assetPath = relative.generic_string();
	if (assetPath.rfind("Assets/", 0) != 0) {
		return {};
	}
	return assetPath;
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
		candidate.stemLower = Algorithm::ToLower(candidate.fullPath.stem().string());
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
		// 1. Assets/Textures/<modelStem>/...
		// 2. .dds
		// 3. それ以外
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

void Engine::TextureAssetResolver::Build(const std::filesystem::path& modelFullPath) {

	candidatesByStem_.clear();
	preferredFolder_.clear();

	if (!std::filesystem::exists(texturesRoot_) || !std::filesystem::is_directory(texturesRoot_)) {
		return;
	}

	// モデルファイルと同名のサブフォルダを優先的に検索
	const std::string modelStem = modelFullPath.stem().string();
	preferredFolder_ = texturesRoot_ / modelStem;
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

	const std::string stemLower = NormalizeStem(importedReference);
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