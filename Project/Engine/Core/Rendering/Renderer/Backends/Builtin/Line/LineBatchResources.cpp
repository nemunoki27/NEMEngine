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
		buffer.CreateBuffer(graphicsCore.GetDXObject().GetResourceRetirement(), device);
	}
}

void Engine::LineBatchResources::UploadVertices(GraphicsCore& graphicsCore, const std::vector<LineVertex>& vertices) {

	if (vertices.size() > UINT32_MAX / sizeof(LineVertex)) throw std::length_error("Lineの頂点数が多すぎます");
	vertexCount_ = static_cast<uint32_t>(vertices.size());
	if (vertexCount_ == 0) {
		return;
	}

	// 容量不足なら作り直して拡張する、再確保を減らすため必要数の2倍を確保する
	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	if (capacities_[frameIndex] < vertexCount_) {

		const uint32_t capacity = vertexCount_ > UINT32_MAX / sizeof(LineVertex) / 2 ? vertexCount_ : vertexCount_ * 2;
		vertexBuffers_[frameIndex].CreateBuffer(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(), capacity);
		capacities_[frameIndex] = capacity;
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

//============================================================================
//	LineBatchResources classMethods
//============================================================================

namespace Engine {

	const D3D12_VERTEX_BUFFER_VIEW& LineBatchResources::GetVBV() const {

		return vertexBuffers_[GraphicsFrameState::GetCurrentIndex()]
			.GetVertexBufferView();
	}

	D3D12_GPU_VIRTUAL_ADDRESS LineBatchResources::GetViewGPUAddress() const {

		return viewBuffers_[GraphicsFrameState::GetCurrentIndex()]
			.GetResource()->GetGPUVirtualAddress();
	}
}
