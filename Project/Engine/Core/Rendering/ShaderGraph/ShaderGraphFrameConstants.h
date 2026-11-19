#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <vector>

namespace Engine {

	class GraphicsCore;
	struct SystemContext;

	// ShaderGraphの時間と描画Cameraを渡す転送型
	struct ShaderGraphTimeConstantsGPU {

		float time = 0.0f;
		float deltaTime = 0.0f;
		float smoothDeltaTime = 0.0f;
		float unscaledTime = 0.0f;
		Matrix4x4 worldToView = Matrix4x4::Identity();
		Matrix4x4 viewToWorld = Matrix4x4::Identity();
		Matrix4x4 viewToClip = Matrix4x4::Identity();
		Matrix4x4 clipToView = Matrix4x4::Identity();
		Matrix4x4 worldToClip = Matrix4x4::Identity();
		Matrix4x4 clipToWorld = Matrix4x4::Identity();
		Vector4 cameraPosition{0.0f, 0.0f, 0.0f, 1.0f};
		Vector4 cameraDirection{0.0f, 0.0f, 1.0f, 0.0f};
		Vector4 cameraProjection{0.0f, 0.1f, 1000.0f, 1.0f};
		Vector4 screenSize{128.0f, 128.0f, 2.0f, 2.0f};
		Vector4 viewport{0.0f, 0.0f, 128.0f, 128.0f};
	};
	static_assert(sizeof(ShaderGraphTimeConstantsGPU) == 480);

	// 確定済みCameraと時間からフレーム定数を作る
	ShaderGraphTimeConstantsGPU BuildShaderGraphFrameConstants(const SystemContext& system,
		const ResolvedRenderView& view, RenderCameraDomain domain = RenderCameraDomain::Perspective,
		Vector4 viewport = {});

	//============================================================================
	//	ShaderGraphFrameConstantBuffer class
	//	Cameraごとのフレーム定数をGPUへ転送して同じ描画で再利用する
	//============================================================================
	class ShaderGraphFrameConstantBuffer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// フレームをまたぐ参照を破棄する
		void BeginFrame();
		// 保持した転送領域を回収する
		void Release();
		// 指定した描画Cameraの転送先を取得する
		D3D12_GPU_VIRTUAL_ADDRESS GetAddress(GraphicsCore& graphicsCore, const SystemContext& system,
			const ResolvedRenderView& view, RenderCameraDomain domain = RenderCameraDomain::Perspective,
			Vector4 viewport = {});

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		struct FrameEntry {

			ShaderGraphTimeConstantsGPU constants{};
			D3D12_GPU_VIRTUAL_ADDRESS address = 0;
		};

		FrameConstantBufferAllocator allocator_;
		std::vector<FrameEntry> entries_;
	};
} // Engine
