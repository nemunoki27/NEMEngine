#include "TextBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Logger/Assert.h>

//============================================================================
//	TextBatchResources classMethods
//============================================================================

void Engine::TextBatchResources::Init(GraphicsCore& graphicsCore) {

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

	// 初期化完了
	initialized_ = true;
}

void Engine::TextBatchResources::CreateQuadBuffers(ID3D12Device* device) {

	// 頂点データを作成
	std::vector<TextVertex> vertices = {
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

void Engine::TextBatchResources::UpdateView(const ResolvedRenderView& view) {

	// 定数バッファにビュー行列を転送する
	TextViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Orthographic)) {

		constants.viewProjection = camera->matrices.viewProjectionMatrix;
	}
	view_.Upload(constants);
}

void Engine::TextBatchResources::UploadGlyphs(const std::vector<TextVSInstanceData>& vsGlyphs,
	const std::vector<TextPSInstanceData>& psGlyphs) {

	const uint32_t glyphCount = static_cast<uint32_t>((std::min)(vsGlyphs.size(), psGlyphs.size()));
	instanceCount_ = glyphCount;

	if (glyphCount == 0) {
		vsData_.Upload(std::span<const TextVSInstanceData>{});
		psData_.Upload(std::span<const TextPSInstanceData>{});
		return;
	}

	vsData_.Upload(std::span<const TextVSInstanceData>(vsGlyphs.data(), glyphCount));
	psData_.Upload(std::span<const TextPSInstanceData>(psGlyphs.data(), glyphCount));
}