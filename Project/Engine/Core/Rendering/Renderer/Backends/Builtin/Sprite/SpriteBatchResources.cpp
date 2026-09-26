#include "SpriteBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
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

void Engine::SpriteBatchResources::CreateQuadBuffers(ID3D12Device* device, BufferUploadService& uploadService) {

	// 頂点データを作成
	std::vector<SpriteVertex> vertices = {
		// 左下
		{ Vector2(0.0f, 1.0f), Vector2(0.0f, 1.0f) },
		// 左上
		{ Vector2(0.0f, 0.0f), Vector2(0.0f, 0.0f) },
		// 右下
		{ Vector2(1.0f, 1.0f), Vector2(1.0f, 1.0f) },
		// 右上
		{ Vector2(1.0f, 0.0f), Vector2(1.0f, 0.0f) },
	};
	// インデックスデータを作成
	std::vector<uint32_t> indices = {
		0, 1, 2,
		1, 3, 2
	};
	// SpriteのQuad形状は初期化後に変わらないため、DEFAULT heapへ置きUploadServiceで初期転送する
	vertexBuffer_.Create(device, uploadService, std::span<const SpriteVertex>(vertices.data(), vertices.size()));
	indexBuffer_.Create(device, uploadService, std::span<const uint32_t>(indices.data(), indices.size()));
	// 固定Quadの転送はInit中に完結させ、以後の描画ではDEFAULT heapだけを参照する
	uploadService.SubmitBatch();
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
