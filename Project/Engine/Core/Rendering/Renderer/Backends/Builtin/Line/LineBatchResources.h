#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Buffers/VertexBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <array>
#include <vector>

namespace Engine {

	//============================================================================
	//	LineBatchResources structures
	//============================================================================
	// GS版ラインシェーダの頂点入力、POSITION/THICKNESS0/COLOR0に対応する
	struct LineVertex {

		Vector3 position;
		// ライン半幅
		float thickness = 1.0f;
		Color4 color;
	};

	// ラインシェーダのViewConstants b0
	struct LinePassConstants {

		Matrix4x4 viewMatrix = Matrix4x4::Identity();
		Matrix4x4 projectionMatrix = Matrix4x4::Identity();

		Vector2 viewportSize = Vector2::AnyInit(1.0f);
		float nearClip = 0.01f;
		float feather = 1.25f;
	};

	//============================================================================
	//	LineBatchResources class
	//	ラインバッチ1回分の頂点バッファとViewConstantsを保持する
	//============================================================================
	class LineBatchResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		LineBatchResources() = default;
		~LineBatchResources() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// 頂点を転送する、容量不足なら頂点バッファを作り直して拡張する
		void UploadVertices(GraphicsCore& graphicsCore, const std::vector<LineVertex>& vertices);
		// ViewConstantsを更新する
		void UpdateView(const ResolvedCameraView& camera, const ResolvedRenderView& view);

		//--------- accessor -----------------------------------------------------

		const D3D12_VERTEX_BUFFER_VIEW& GetVBV() const {
			return vertexBuffers_[GraphicsFrameState::GetCurrentIndex()]
				.GetVertexBufferView();
		}
		D3D12_GPU_VIRTUAL_ADDRESS GetViewGPUAddress() const {
			return viewBuffers_[GraphicsFrameState::GetCurrentIndex()]
				.GetResource()->GetGPUVirtualAddress();
		}
		uint32_t GetVertexCount() const { return vertexCount_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// AA用フェザー幅
		static constexpr float kLineAAFeather = 1.25f;

		// 頂点バッファと現在確保中の頂点容量
		std::array<VertexBuffer<LineVertex>,
			kGraphicsFrameContextCount> vertexBuffers_{};
		std::array<uint32_t,
			kGraphicsFrameContextCount> capacities_{};
		// 直近で転送した頂点数
		uint32_t vertexCount_ = 0;

		// ViewConstants b0
		std::array<DxConstBuffer<LinePassConstants>,
			kGraphicsFrameContextCount> viewBuffers_{};
	};
} // Engine
