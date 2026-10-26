#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleGPUFrameInputs.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderFrameTypes.h>

namespace Engine {

	struct SceneInstance;
	struct SceneExecutionContext;

	// 表示状態から独立して粒子が参照するCamera入力を保持する
	class ParticleCameraDepthPreparation {
	public:
		ParticleCameraDepthPreparation();
		~ParticleCameraDepthPreparation();
		// 指定Cameraの現在深度と前回の描画入力を準備する
		ParticleGPUFrameInputs Prepare(GraphicsCore& graphics, const RenderFrameRequest& request,
			const SceneInstance* scene, std::span<const ResolvedRenderView> gameViews,
			Entity mainCamera, const RenderPipelineDeps& deps, const RenderBufferRegistry& sharedBuffers);
		// 表示変換前の色と対応する深度を次回更新へ保持する
		void Capture(GraphicsCore& graphics, const SceneExecutionContext& context);
		bool NeedsCapture(Entity camera) const;
		// Device終了前にCamera入力の所有を回収へ渡す
		void Clear();
	private:
		struct Data;
		std::unique_ptr<Data> data_;
	};
}
