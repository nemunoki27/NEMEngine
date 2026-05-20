#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessParameterLayout.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

// c++
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;
	class PipelineState;
	class PipelineStateCache;
	class RenderAssetLibrary;
	struct RenderFrameRequest;
	struct SceneExecutionContext;

	//============================================================================
	//	PostProcess structures
	//============================================================================

	struct PostProcessFrameConstants {

		Vector2 resolution{};
		Vector2 invResolution{};
		float time = 0.0f;
		float deltaTime = 0.0f;
		uint32_t frameIndex = 0;
		float padding = 0.0f;
	};

	struct PostProcessExecutionDesc {

		AssetID material{};
		std::string passName = "PostProcess";
		RenderTargetSetReference source;
		RenderTargetSetReference dest;
		std::unordered_map<std::string, std::string> extraSources;
		ComputeDispatchMode dispatchMode = ComputeDispatchMode::FromDestSize;
		uint32_t groupCountX = 1;
		uint32_t groupCountY = 1;
		uint32_t groupCountZ = 1;
	};

	//============================================================================
	//	PostProcessExecutor class
	//	ComputeShader版PostProcessの実行を担当するクラス。
	//============================================================================
	class PostProcessExecutor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PostProcessExecutor() = default;
		~PostProcessExecutor() = default;

		// フレームごとの時間情報を更新する
		void BeginFrame(float deltaTime);
		// 内部バッファを破棄する
		void Release();

		// Compute PostProcessを実行する
		bool Execute(GraphicsCore& graphicsCore, const RenderFrameRequest& request,
			const SceneExecutionContext& context, RenderAssetLibrary& assetLibrary,
			PipelineStateCache& pipelineCache, const PostProcessExecutionDesc& desc);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		PostProcessConstantBufferAllocator constantBufferAllocator_{};
		std::unordered_map<const PipelineState*, PostProcessParameterLayout> parameterLayoutCache_{};
		float elapsedTime_ = 0.0f;
		uint32_t frameIndex_ = 0;
	};
} // Engine
