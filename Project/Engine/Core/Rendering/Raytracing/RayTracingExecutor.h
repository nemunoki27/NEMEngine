#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphFrameConstants.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingStructures.h>

// c++
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Engine {

	class GraphicsCore;
	class RenderAssetLibrary;
	class RaytracingPipelineStateCache;
	class RenderTexture2D;
	struct RenderFeaturePassRuntimeOverride;
	struct SceneExecutionContext;

	// DispatchRaysへ解決済みのグラフリソースを渡す
	struct RayTracingExecutionResources {

		std::unordered_map<std::string, std::string> inputs{};
		std::unordered_map<std::string, RenderTexture2D*> outputs{};
		RenderTexture2D* dispatchTarget = nullptr;
	};

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
			const RenderFeaturePassSettings& pass,
			const RayTracingExecutionResources& resources,
			const RenderFeaturePassRuntimeOverride* runtimeOverride = nullptr);

		void ClearParameterLayoutCache();

		// 最後に実行したDXRシェーダーのReflectionを取得する
		const ShaderReflectionInfo* GetLastReflection() const { return lastReflection_; }
		uint64_t GetLastExecutedPipelineID() const { return lastExecutedPipelineID_; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		UUID preparedScene_{};
		std::vector<RaytracingHitMaterial> geometryMaterials_;
		std::vector<RaytracingHitMaterial> hitMaterials_;
		std::vector<uint8_t> hitRecords_;
		uint64_t hitRecordPipeline_ = 0;

		FrameConstantBufferAllocator constantBufferAllocator_{};
		ShaderGraphFrameConstantBuffer shaderGraphFrameConstants_;
		std::unordered_map<uint64_t, MaterialParameterLayout>
			parameterLayoutCache_{};
		uint64_t allocatorFrameSerial_ = 0;
		std::unordered_set<std::string> diagnostics_{};
		const ShaderReflectionInfo* lastReflection_ = nullptr;
		uint64_t lastExecutedPipelineID_ = 0;
		std::vector<RenderTexture2D*> outputTextureScratch_{};

		// 同一エフェクトの同一エラーを1度だけ出力する
		void ReportFailure(const RenderFeaturePassSettings& pass,
			std::string_view reason);
	};
} // Engine
