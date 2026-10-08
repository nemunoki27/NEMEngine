#pragma once

//============================================================================
//	include
//============================================================================
#include "RaytracingPipelineState.h"
#include <memory>

namespace Engine {

	class RaytracingPipelineBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 完成したDXRパイプラインを返す
		static std::unique_ptr<RaytracingPipelineState> Create(ID3D12Device8* device, DxShaderCompiler* compiler,
			const PipelineVariantDesc& variant, const ShaderAsset& shaderAsset,
			const PipelineStaticSamplerOverrideSet* samplerOverrides = nullptr);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		// パイプラインを構築する
		static bool Build(RaytracingPipelineState& state, ID3D12Device8* device, DxShaderCompiler* compiler,
			const PipelineVariantDesc& variant, const ShaderAsset& shaderAsset,
			const PipelineStaticSamplerOverrideSet* samplerOverrides);
		// RootSignatureを構築する
		static bool BuildGlobalRootSignature(RaytracingPipelineState& state, ID3D12Device8* device,
			const std::vector<const CompiledShader*>& shaders,
			const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers);
		// StateObjectを構築する
		static bool BuildStateObject(RaytracingPipelineState& state, ID3D12Device8* device, DxShaderCompiler* compiler,
			const PipelineVariantDesc& variant, const ShaderAsset& shaderAsset,
			const PipelineStaticSamplerOverrideSet* samplerOverrides);
		// ShaderTableを構築する
		static bool BuildShaderTable(RaytracingPipelineState& state, ID3D12Device8* device,
			const std::vector<std::wstring>& rayGenerationExports,
			const std::vector<std::wstring>& missExports,
			const std::vector<std::wstring>& hitGroupExports,
			const std::vector<std::wstring>& callableExports);
	};
}
