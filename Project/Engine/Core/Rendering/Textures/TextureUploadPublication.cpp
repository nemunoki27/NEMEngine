#include "TextureUploadService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>

//============================================================================
//	TextureUploadService publicationMethods
//============================================================================
void Engine::TextureUploadService::PublishUpload(const CompletedRequest& completed, GPUTextureResource& uploaded) {

	const auto& job = completed.texture;
	std::scoped_lock lock(mutex_);
	queuedKeys_.erase(job.key);
	// GPU転送中に変更された要求も公開しない
	if (!IsCurrentRequest(completed)) {

		if (uploaded.valid) srvDescriptor_->Retire(uploaded.srvIndex, uploaded.resource);
		return;
	}
	// 再読込に失敗した場合は公開済みの画像を保持する
	if (readyTextures_.contains(job.key) && !uploaded.valid) return;
	if (!uploaded.valid) {

		failedKeys_.insert(job.key);
		return;
	}
	try {

		uploaded.ownership = std::make_shared<const uint8_t>(0);
		textureAccessTimes_[job.key] = std::chrono::steady_clock::now();
		// 名前の準備も差し替え前に完了する
		uploaded.textureName = job.key;
		uploaded.resource->SetName(Algorithm::ConvertString(job.key).c_str());
		srvDescriptor_->UpdateResourceName(uploaded.srvIndex, uploaded.resource.Get());
		const auto desc = uploaded.resource->GetDesc();
		Logger::Output(LogType::Engine,
			"[TextureLoad][GPU] key={} srvIndex={} size={}x{} format={} status=準備完了",
			job.key, uploaded.srvIndex, static_cast<uint32_t>(desc.Width), desc.Height, static_cast<uint32_t>(desc.Format));
		auto existing = readyTextures_.find(job.key);
		if (existing != readyTextures_.end()) {

			// 使用中の旧DescriptorとResourceを一組で保持する
			srvDescriptor_->Retire(existing->second.srvIndex, existing->second.resource);
			readyAllocationBytes_ -= existing->second.allocationBytes;
			existing->second = std::move(uploaded);
		} else {

			readyTextures_.emplace(job.key, std::move(uploaded));
		}
		// 公開の変更時だけ割当量を更新する
		readyAllocationBytes_ += readyTextures_.at(job.key).allocationBytes;
	} catch (...) {

		// 公開前の新Descriptorだけを戻す
		srvDescriptor_->Free(uploaded.srvIndex);
		uploaded.srvIndex = UINT32_MAX;
		uploaded.valid = false;
		throw;
	}
	failedKeys_.erase(job.key);
	++contentRevision_;
}
