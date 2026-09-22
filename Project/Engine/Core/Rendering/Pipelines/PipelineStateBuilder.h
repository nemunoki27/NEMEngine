#pragma once

//============================================================================
//	include
//============================================================================
#include "PipelineState.h"

#include <memory>

namespace Engine {

	//============================================================================
	//	PipelineStateBuilder class
	//	全stageの生成を完了してからPipelineを公開する
	//============================================================================
	class PipelineStateBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		static std::unique_ptr<PipelineState> CreateGraphics(ID3D12Device8* device, DxShaderCompiler* compiler,
			const GraphicsPipelineDesc& desc, const ShaderAsset* metadata = nullptr);
		static std::unique_ptr<PipelineState> CreateCompute(ID3D12Device8* device, DxShaderCompiler* compiler,
			const ComputePipelineDesc& desc, const ShaderAsset* metadata = nullptr);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// Graphicsの全BlendModeを生成する
		static bool BuildGraphics(PipelineState& state, ID3D12Device8* device, DxShaderCompiler* compiler,
			const GraphicsPipelineDesc& desc);
		// ComputeのRootとPSOを生成する
		static bool BuildCompute(PipelineState& state, ID3D12Device8* device, DxShaderCompiler* compiler,
			const ComputePipelineDesc& desc);
	};
}
