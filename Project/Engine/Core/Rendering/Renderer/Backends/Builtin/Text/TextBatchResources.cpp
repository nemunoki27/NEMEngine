#include "TextBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/QuadGeometry.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

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
	CreateQuadBuffers(device, graphicsCore.GetBufferUploadService());
	view_.Init(graphicsCore.GetDXObject().GetResourceRetirement(), device);
	vsData_.Init(device, srvDescriptor);
	psData_.Init(device, srvDescriptor);
	vsData_.EnsureCapacity(256);
	psData_.EnsureCapacity(256);

	// 初期化完了
	initialized_ = true;
}

void Engine::TextBatchResources::CreateQuadBuffers(
	ID3D12Device* device, BufferUploadService& uploadService) {

	// 単位矩形の静的Bufferを作成
	QuadGeometry::CreateBuffers(device, uploadService, vertexBuffer_, indexBuffer_);
}

void Engine::TextBatchResources::UpdateView(const ResolvedRenderView& view, RenderCameraDomain cameraDomain) {

	// 定数バッファにビュー行列を転送する
	TextViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(cameraDomain)) {

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
