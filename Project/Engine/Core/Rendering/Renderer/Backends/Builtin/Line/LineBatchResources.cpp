#include "LineBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>

//============================================================================
//	LineBatchResources classMethods
//============================================================================
void Engine::LineBatchResources::Init(GraphicsCore& graphicsCore) {

	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();

	// ViewConstantsは固定サイズなので最初に確保する
	for (DxConstBuffer<LinePassConstants>& buffer : viewBuffers_) {
		buffer.CreateBuffer(device);
	}
}

void Engine::LineBatchResources::UploadVertices(GraphicsCore& graphicsCore, const std::vector<LineVertex>& vertices) {

	vertexCount_ = static_cast<uint32_t>(vertices.size());
	if (vertexCount_ == 0) {
		return;
	}

	// 容量不足なら作り直して拡張する、再確保を減らすため必要数の2倍を確保する
	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	if (capacities_[frameIndex] < vertexCount_) {

		capacities_[frameIndex] = vertexCount_ * 2;
		vertexBuffers_[frameIndex].CreateBuffer(
			graphicsCore.GetDXObject().GetDevice(), capacities_[frameIndex]);
	}
	vertexBuffers_[frameIndex].TransferData(vertices);
}

void Engine::LineBatchResources::UpdateView(const ResolvedCameraView& camera, const ResolvedRenderView& view) {

	LinePassConstants constants{};
	constants.viewMatrix = camera.matrices.viewMatrix;
	constants.projectionMatrix = camera.matrices.projectionMatrix;
	constants.viewportSize = Vector2(static_cast<float>(view.width), static_cast<float>(view.height));
	constants.nearClip = camera.nearClip;
	constants.feather = kLineAAFeather;
	viewBuffers_[GraphicsFrameState::GetCurrentIndex()]
		.TransferData(constants);
}
