#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Decals/DecalTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>

// c++
#include <memory>
#include <unordered_map>

namespace Engine {

	struct RenderPipelineDeps;

	//============================================================================
	//	DecalPass class
	//	投影情報をGBufferと反射へ渡す固定描画工程
	//============================================================================
	class DecalPass : public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit DecalPass(const RenderPipelineDeps& deps);
		~DecalPass() override = default;

		// 各Viewの表面へ投影する
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;
		// 閉じたViewのGPU資源を回収する
		void RetainViews(const std::unordered_set<std::string>& activeViews) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::Decal; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct State {

			std::vector<DecalDrawItem> items;
			std::vector<DecalGPUData> gpuData;
			std::vector<DecalReceiverRange> ranges;
			std::vector<uint32_t> indices;
			StructuredInstanceBuffer<DecalGPUData> decals{"gDecals"};
			StructuredInstanceBuffer<DecalReceiverRange> receivers{"gDecalReceiverRanges"};
			StructuredInstanceBuffer<uint32_t> candidates{"gDecalReceiverIndices"};
			std::unique_ptr<RenderTexture2D> originalNormal;
			uint32_t width = 0;
			uint32_t height = 0;
			bool initialized = false;
		};
		struct DrawConstants {

			Vector3 cameraPosition{};
			float pixelWorldScale = 0.0f;
			uint32_t decalIndex = 0;
			uint32_t orthographic = 0;
			uint32_t _pad[2]{};
		};

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
		std::unique_ptr<PipelineState> pipeline_;
		std::unordered_map<std::string, std::unique_ptr<State>> states_;
		FrameConstantBufferAllocator constants_{4096};
		uint64_t constantFrameSerial_ = UINT64_MAX;
		RegistryAutoBindTable autoBind_;

		//--------- functions ----------------------------------------------------

		// Geometryごとの投影候補と転送値を確定する
		void Prepare(GraphicsCore& graphicsCore, SceneExecutionContext& context, State& state);
		// 必要なViewだけ法線の退避先を用意する
		void EnsureNormalTarget(GraphicsCore& graphicsCore, State& state, uint32_t width, uint32_t height);
		// チャンネルごとの合成状態を持つPipelineを生成する
		void EnsurePipeline(GraphicsCore& graphicsCore);
	};
}
