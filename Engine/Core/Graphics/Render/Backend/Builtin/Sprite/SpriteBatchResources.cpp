#include "SpriteBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Debug/Assert.h>

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
	CreateQuadBuffers(device);
	view_.Init(device);
	vsData_.Init(device, srvDescriptor);
	psData_.Init(device, srvDescriptor);
	vsData_.EnsureCapacity(256);
	psData_.EnsureCapacity(256);
	vsScratch_.reserve(256);
	psScratch_.reserve(256);

	// 初期化完了
	initialized_ = true;
}

void Engine::SpriteBatchResources::CreateQuadBuffers(ID3D12Device* device) {

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
	// バッファを作成してデータを転送
	vertexBuffer_.CreateBuffer(device, static_cast<UINT>(vertices.size()));
	vertexBuffer_.TransferData(vertices);
	indexBuffer_.CreateBuffer(device, static_cast<UINT>(indices.size()));
	indexBuffer_.TransferData(indices);
}

void Engine::SpriteBatchResources::UpdateView(const ResolvedRenderView& view) {

	// 定数バッファにビュー行列を転送する
	SpriteViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Orthographic)) {

		constants.viewProjection = camera->matrices.viewProjectionMatrix;
	}
	view_.Upload(constants);
}

void Engine::SpriteBatchResources::UploadInstances(const RenderSceneBatch& batch,
	const std::span<const RenderItem* const>& items) {

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
			instance.worldMatrix = item->worldMatrix;
			instance.size = payload->size;
			instance.pivot = payload->pivot;
			// インスタンスデータを追加する
			vsScratch_.emplace_back(instance);
		}
		// PS
		{
			SpritePSInstanceData instance{};
			instance.color = payload->color;
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