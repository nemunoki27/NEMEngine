#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshShaderSharedTypes.h>
#include <array>

namespace Engine {

	struct RenderDrawContext;
	struct MeshViewConstants {

		// 実際に描画するビューの行列
		Matrix4x4 viewProjection = Matrix4x4::Identity();
		Matrix4x4 previousViewProjection = Matrix4x4::Identity();
		// カリング判定に使うビューの行列でSceneViewではGameViewの行列になる
		Matrix4x4 cullingViewProjection = Matrix4x4::Identity();
		// Contribution CullingでカリングカメラのView空間へ変換する
		Matrix4x4 cullingView = Matrix4x4::Identity();
		// NormalCone判定で使用するカリングカメラ位置
		Vector3 cullingCameraPos = Vector3::AnyInit(0.0f);
		// Nearより手前に球がかかる場合はContribution判定を安全側で無効にする
		float cullingNearClip = 0.001f;
		// Hi-Z判定で球の最前面を求めるカリングカメラ前方
		Vector3 cullingCameraForward = Vector3(0.0f, 0.0f, 1.0f);
		float _cullingPad0 = 0.0f;
		// 描画先Viewportサイズ
		Vector2 viewSize = Vector2::AnyInit(1.0f);
		// カリング対象ViewportサイズでSceneView表示時もGameViewサイズを使う
		Vector2 cullingViewSize = Vector2::AnyInit(1.0f);
		// Projection行列のX/Y倍率でViewProjectionから取るとカメラ回転で値が崩れる
		Vector2 cullingProjectionScale = Vector2::AnyInit(1.0f);
		Vector2 _pad0 = Vector2::AnyInit(0.0f);
		// PBRライト計算に使う、実際に描画しているビューのカメラ位置
		Vector3 renderCameraPos = Vector3::AnyInit(0.0f);
		uint32_t frameSerial = 0;
	};

	struct MeshIndirectArgsConstants {

		// ExecuteIndirectのDrawIndexedInstancedに渡すIndex数
		uint32_t indexCount = 0;
		uint32_t _pad[3] = { 0, 0, 0 };
	};

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
		void UpdateView(const ResolvedRenderView& view, const ResolvedRenderView* cullingView);
		// Draw用の定数を確定する
		void UpdateDrawConstants(const RenderDrawContext& drawContext, const MeshGPUResource& gpuMesh,
			uint32_t subMeshIndex, uint32_t subMeshGroupIndex, ID3D12Device* device, uint32_t instanceCount,
			const OutlineBatchMetrics& outlineMetrics, float maxDisplacement);
		// 間接描画用の定数を更新する
		void UpdateIndexedIndirectArgsConstants(uint32_t indexCount, ID3D12Device* device);
		D3D12_GPU_VIRTUAL_ADDRESS GetViewGPUAddress(RenderViewKind kind) const { return view_[ToViewIndex(kind)].GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetDrawGPUAddress() const { return drawGPUAddress_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetMaskGPUAddress() const { return screenSpaceOutlineMaskGPUAddress_; }
		D3D12_GPU_VIRTUAL_ADDRESS GetIndirectGPUAddress() const { return indirectArgsGPUAddress_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		std::array<ViewConstantBuffer<MeshViewConstants>, 2> view_ = {
			ViewConstantBuffer<MeshViewConstants>{ "ViewConstants" },
			ViewConstantBuffer<MeshViewConstants>{ "ViewConstants" }
		};

		GraphicsResourceRetirement* retirement_ = nullptr;
		FrameConstantBufferAllocator dynamicConstantAllocator_{};
		uint64_t dynamicConstantFrameSerial_ = 0;
		std::array<uint64_t, 2> viewUploadFrameSerials_ = { 0, 0 };
		std::array<Matrix4x4, 2> previousViewProjections_ = {
			Matrix4x4::Identity(), Matrix4x4::Identity()
		};
		std::array<bool, 2> previousViewValid_ = { false, false };
		D3D12_GPU_VIRTUAL_ADDRESS drawGPUAddress_ = 0;
		D3D12_GPU_VIRTUAL_ADDRESS screenSpaceOutlineMaskGPUAddress_ = 0;
		D3D12_GPU_VIRTUAL_ADDRESS indirectArgsGPUAddress_ = 0;

		// Frame開始時に定数の割当位置を戻す
		void BeginDynamicConstantsFrame();
		// Viewの格納位置を取得する
		static constexpr size_t ToViewIndex(RenderViewKind kind) { return static_cast<size_t>(kind); }
	};
}
