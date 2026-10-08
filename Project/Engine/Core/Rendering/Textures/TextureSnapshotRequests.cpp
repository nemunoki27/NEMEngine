#include "TextureUploadService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <format>
#include <limits>
#include <stdexcept>

//============================================================================
//	TextureUploadService classMethods
//============================================================================
std::wstring Engine::TextureUploadService::NormalizeFilePath(const std::filesystem::path& path) {

	return Algorithm::ToLowerW(path.lexically_normal().generic_wstring());
}

uint64_t Engine::TextureUploadService::GetFileReloadRevision(const std::filesystem::path& fullPath) const {

	const auto key = NormalizeFilePath(fullPath);
	std::scoped_lock lock(mutex_);
	const auto found = fileReloadRevisions_.find(key);
	return found == fileReloadRevisions_.end() ? 0 : found->second;
}

std::string Engine::TextureUploadService::RequestSnapshot(TextureFileRequestDesc description) {

	if (!description.snapshotBytes || description.snapshotBytes->empty()) {
		return {};
	}
	{
		std::scoped_lock lock(mutex_);
		if (nextSnapshotID_ == std::numeric_limits<uint64_t>::max()) {
			throw std::overflow_error("Textureの固定世代が上限に達しました");
		}
		// サービス再初期化後も以前のキーを再利用しない
		description.key = std::format("NEM:TextureSnapshot:{}", nextSnapshotID_++);
	}
	RequestTextureFile(description);
	return description.key;
}

void Engine::TextureUploadService::ReleaseSnapshot(const std::string& key) {

	std::scoped_lock lock(mutex_);
	const auto request = keyRequests_.find(key);
	if (request == keyRequests_.end() || !request->second.description.snapshotBytes) {
		return;
	}
	const auto texture = readyTextures_.find(key);
	if (texture != readyTextures_.end()) {
		// 描画中のDescriptorもGPU完了まで保持する
		srvDescriptor_->Retire(texture->second.srvIndex, texture->second.resource);
		readyTextures_.erase(texture);
		++contentRevision_;
	}
	// 受付済みの結果は要求照合で失効させる
	keyRequests_.erase(request);
	queuedKeys_.erase(key);
	failedKeys_.erase(key);
	deferredReloadKeys_.erase(key);
}
