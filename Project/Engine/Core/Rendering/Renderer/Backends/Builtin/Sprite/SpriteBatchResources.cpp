#include "SpriteBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/QuadGeometry.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

//============================================================================
//	SpriteBatchResources classMethods
//============================================================================
void Engine::SpriteBatchResources::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor* srvDescriptor = &graphicsCore.GetSRVDescriptor();

	// バッファ作成
	CreateQuadBuffers(device, graphicsCore.GetBufferUploadService());
	view_.Init(graphicsCore.GetDXObject().GetResourceRetirement(), device);
	vsData_.Init(device, srvDescriptor);
	psData_.Init(device, srvDescriptor);
	vsData_.EnsureCapacity(256);
	psData_.EnsureCapacity(256);
	vsScratch_.reserve(256);
	psScratch_.reserve(256);

	// 初期化完了
	initialized_ = true;
}

void Engine::SpriteBatchResources::CreateQuadBuffers(
	ID3D12Device* device, BufferUploadService& uploadService) {

	// 単位矩形の静的Bufferを作成
	QuadGeometry::CreateBuffers(device, uploadService, vertexBuffer_, indexBuffer_);
}

void Engine::SpriteBatchResources::UpdateView(const ResolvedRenderView& view, RenderCameraDomain cameraDomain) {

	// 定数バッファにビュー行列を転送する
	SpriteViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(cameraDomain)) {

		constants.viewProjection = camera->matrices.viewProjectionMatrix;
	}
	view_.Upload(constants);
}

void Engine::SpriteBatchResources::UploadInstances(const ResolvedRenderView& view,
	const RenderSceneBatch& batch, const std::span<const RenderItem* const>& items) {

	// 描画アイテムからインスタンスデータを構築する
	vsScratch_.clear();
	psScratch_.clear();
	if (vsScratch_.capacity() < items.size()) {
		vsScratch_.reserve(items.size());
	}
	if (psScratch_.capacity() < items.size()) {
		psScratch_.reserve(items.size());
	}
	for (const RenderItem* item : items) {

		const SpriteRenderPayload* payload = batch.GetPayload<SpriteRenderPayload>(*item);
		if (!payload) {
			continue;
		}

		// VS
		{
			SpriteVSInstanceData instance{};
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(*item, view);
			instance.size = payload->size;
			instance.pivot = payload->pivot;
			// インスタンスデータを追加する
			vsScratch_.emplace_back(instance);
		}
		// PS
		{
			SpritePSInstanceData instance{};
			instance.uvMatrix = payload->uvMatrix;
			psScratch_.emplace_back(instance);
		}
	}

	// インスタンス数を設定
	instanceCount_ = static_cast<uint32_t>(vsScratch_.size());

	// インスタンスデータを転送する
	vsData_.Upload(vsScratch_);
	psData_.Upload(psScratch_);
}
