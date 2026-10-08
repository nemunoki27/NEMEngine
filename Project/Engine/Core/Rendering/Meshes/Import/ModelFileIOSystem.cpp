#include "ModelFileIOSystem.h"

//============================================================================
//	include
//============================================================================
#include "GLTFDocumentReferences.h"
#include "GLTFFileReference.h"
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <memory>
#include <stdexcept>

// assimp
#include <assimp/IOStream.hpp>

//============================================================================
//	ModelFileIOSystem classMethods
//============================================================================
Engine::ModelFileIOSystem::ModelFileIOSystem(const std::filesystem::path& model,
	ModelFileDependencyCollector::ModelDependencies* dependencies) :
	modelPath_(NormalizeSeparators(Algorithm::PathToUTF8(model))),
	uriReferences_(GLTFDocumentReferences::IsDocumentPath(model)), dependencies_(dependencies) {

	// 本体と同じ表記で参照元を切り出す
	const size_t separator = modelPath_.find_last_of('/');
	directory_ = separator == std::string::npos ? std::string{} : modelPath_.substr(0, separator + 1);
}

bool Engine::ModelFileIOSystem::Exists(const char* path) const {

	if (!path) {
		return false;
	}
	// 解決済みパスでファイルを確認する
	const auto physical = Algorithm::PathToUTF8(Algorithm::ToFileSystemPath(ResolvePath(path)));
	return fileSystem_.Exists(physical.c_str());
}

Assimp::IOStream* Engine::ModelFileIOSystem::Open(const char* path, const char* mode) {

	if (!path || !mode) {
		return nullptr;
	}
	// 読込先の実ファイルを開く
	const auto resolved = ResolvePath(path);
	const auto physical = Algorithm::PathToUTF8(Algorithm::ToFileSystemPath(resolved));
	std::unique_ptr<Assimp::IOStream> stream(fileSystem_.Open(physical.c_str(), mode));
	if (stream && dependencies_ && mode[0] == 'r') {
		// 通常読込と同じ実ファイルを収集する
		dependencies_->AddFile(resolved);
	}
	return stream.release();
}

bool Engine::ModelFileIOSystem::ComparePaths(const char* first, const char* second) const {

	// 実ファイルの表記を揃えて比較する
	const auto firstPath = Algorithm::PathToUTF8(Algorithm::ToFileSystemPath(ResolvePath(first)));
	const auto secondPath = Algorithm::PathToUTF8(Algorithm::ToFileSystemPath(ResolvePath(second)));
	return fileSystem_.ComparePaths(firstPath.c_str(), secondPath.c_str());
}

char Engine::ModelFileIOSystem::getOsSeparator() const {

	return fileSystem_.getOsSeparator();
}

void Engine::ModelFileIOSystem::Close(Assimp::IOStream* stream) {

	// 標準の読込処理へStreamを返す
	fileSystem_.Close(stream);
}

std::string Engine::ModelFileIOSystem::NormalizeSeparators(std::string_view path) {

	std::string normalized(path);
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	return normalized;
}

std::filesystem::path Engine::ModelFileIOSystem::ResolvePath(std::string_view path) const {

	const auto normalized = NormalizeSeparators(path);
	if (!uriReferences_ || normalized == modelPath_) {
		return Algorithm::PathFromUTF8(normalized);
	}
	if (!normalized.starts_with(directory_)) {
		throw std::invalid_argument("glTFの外部参照の基準を解決できません");
	}
	// フォルダー名の%を復号せず参照文字列だけを扱う
	return Algorithm::PathFromUTF8(directory_) / GLTFFileReference::Decode(normalized.substr(directory_.size()));
}
