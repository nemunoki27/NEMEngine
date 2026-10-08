#pragma once

//============================================================================
//	include
//============================================================================
#include "MeshBatchTypes.h"
#include <Engine/Core/Rendering/Renderer/Views/RenderCameraHistory.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>

// c++
#include <array>

namespace Engine {

	struct RenderDrawContext;
	// 輪郭の膨張と深度移動の範囲
	struct OutlineBatchMetrics {

		float maxModelExpansion = 0.0f;
		float maxAbsCameraZOffset = 0.0f;
		bool hasScreenPixelWidth = false;
	};
	//============================================================================
	//	MeshBatchViewResources class
	//	ViewとDrawの定数を描画単位で保持する
	//============================================================================
	class MeshBatchViewResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// View用の定数領域を初期化する
		void Init(GraphicsResourceRetirement& retirement, ID3D12Device* device);
		// 描画ごとの定数領域を解放する
		void Release();
		// Viewの行列と履歴を更新する
		void UpdateView(const ResolvedRenderView& view, const ResolvedRenderView* cullingView,
			const ResolvedRenderView* lodView = nullptr);
		// Draw用の定数を確定する
		void UpdateDrawConstants(const RenderDrawContext& drawContext, const MeshGPUResource& gpuMesh,
			uint32_t subMeshIndex, uint32_t subMeshGroupIndex, ID3D12Device* device, uint32_t instanceCount,
			const OutlineBatchMetrics& outlineMetrics, float maxDisplacement, bool normalConeAllowed);
		// 間接描画用の定数を更新する
		void UpdateIndexedIndirectArgsConstants(uint32_t indexCount, ID3D12Device* device);

		//--------- accessor -----------------------------------------------------

		// View定数のGPUアドレスを取得
		D3D12_GPU_VIRTUAL_ADDRESS GetViewGPUAddress(RenderViewKind kind) const { return view_[ToViewIndex(kind)].GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetDrawGPUAddress() const { return drawGPUAddress_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetMaskGPUAddress() const { return screenSpaceOutlineMaskGPUAddress_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetIndirectGPUAddress() const { return indirectArgsGPUAddress_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::array<ViewConstantBuffer<MeshViewConstants>, 2> view_ = {
			ViewConstantBuffer<MeshViewConstants>{ "ViewConstants" },
			ViewConstantBuffer<MeshViewConstants>{ "ViewConstants" }
		};

		GraphicsResourceRetirement* retirement_ = nullptr;
		FrameConstantBufferAllocator dynamicConstantAllocator_{};
		uint64_t dynamicConstantFrameSerial_ = 0;
		std::array<uint64_t, 2> viewUploadFrameSerials_ = { 0, 0 };
		RenderCameraHistory cameraHistory_{};
		std::array<MeshViewConstants, 2> uploadedViews_{};
		D3D12_GPU_VIRTUAL_ADDRESS drawGPUAddress_ = 0;
		D3D12_GPU_VIRTUAL_ADDRESS screenSpaceOutlineMaskGPUAddress_ = 0;
		D3D12_GPU_VIRTUAL_ADDRESS indirectArgsGPUAddress_ = 0;

		//--------- functions ----------------------------------------------------

		// Frame開始時に定数の割当位置を戻す
		void BeginDynamicConstantsFrame();
		// Viewの格納位置を取得する
		static constexpr size_t ToViewIndex(RenderViewKind kind) { return static_cast<size_t>(kind); }
	};
}
