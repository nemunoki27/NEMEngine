#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	SkyboxPass class
	//	SkyboxRendererComponentのcubemapを背景としてSceneMainへ描くパス
	//============================================================================
	class SkyboxPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit SkyboxPass(const RenderPipelineDeps& deps) : deps_(deps) {
			cbvSlot_ = bindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 0, 0);
		}
		~SkyboxPass() override = default;

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::Skybox; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// b0へ渡すSkybox描画定数
		struct SkyboxConstants {

			Matrix4x4 inverseViewProjection = Matrix4x4::Identity();
			Vector3 cameraPosition = Vector3::AnyInit(0.0f);
			uint32_t cubemapIndex = 0;
			Color4 color = Color4::White();
		};

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;

		PipelineState pipeline_{};
		bool initialized_ = false;

		// 同一フレームでビューごとに複数回描いても定数が上書きされないようプールで持つ
		std::vector<std::unique_ptr<DxConstBuffer<SkyboxConstants>>> constantBuffers_{};
		uint32_t constantBufferIndex_ = 0;

		PipelineBindingCache bindCache_{};
		PipelineBindingCache::SlotID cbvSlot_ = PipelineBindingCache::kInvalidSlot;

		//--------- functions ----------------------------------------------------

		// 初回描画時にパイプラインを生成する
		void EnsurePipeline(GraphicsCore& graphicsCore);
		// フレーム内で再利用する定数バッファを確保する
		DxConstBuffer<SkyboxConstants>& AllocateConstantBuffer(GraphicsCore& graphicsCore);
	};
} // Engine
