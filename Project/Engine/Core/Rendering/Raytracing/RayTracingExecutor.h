#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Raytracing/RayTracingProfileAsset.h>

// c++
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace Engine {

	class GraphicsCore;
	class RenderAssetLibrary;
	class RaytracingPipelineStateCache;
	struct RayTracingEffectRuntimeOverride;
	struct SceneExecutionContext;

	//============================================================================
	//	RayTracingExecutor class
	//	Profileの1エフェクトをDXR Dispatchへ変換する実行器
	//============================================================================
	class RayTracingExecutor {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RayTracingExecutor() = default;
		~RayTracingExecutor() = default;

		void BeginFrame();
		void Release();
		bool Execute(GraphicsCore& graphicsCore,
			const SceneExecutionContext& context,
			RenderAssetLibrary& assetLibrary,
			RaytracingPipelineStateCache& pipelineCache,
			const RayTracingEffectSettings& effect,
			const RayTracingEffectRuntimeOverride* runtimeOverride = nullptr);

		void ClearParameterLayoutCache() {
			parameterLayoutCache_.clear();
			diagnostics_.clear();
		}

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		PostProcessConstantBufferAllocator constantBufferAllocator_{};
		std::unordered_map<uint64_t, MaterialParameterLayout>
			parameterLayoutCache_{};
		uint64_t allocatorFrameSerial_ = 0;
		std::unordered_set<std::string> diagnostics_{};

		// 同一エフェクトの同一エラーを1度だけ出力する
		void ReportFailure(const RayTracingEffectSettings& effect,
			std::string_view reason);
	};
} // Engine
