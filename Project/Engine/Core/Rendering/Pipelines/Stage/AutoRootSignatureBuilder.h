#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/RootSignatureLayout.h>

namespace Engine {

	//============================================================================
	//	AutoRootSignatureBuilder structures
	//============================================================================
	// ルートシグネチャの生成結果
	struct RootSignatureBuildResult {

		ComPtr<ID3D12RootSignature> rootSignature;
		std::vector<RootBindingLocation> bindings;
	};

	//============================================================================
	//	AutoRootSignatureBuilder class
	//	自動でルートシグネチャを生成するクラス
	//============================================================================
	class AutoRootSignatureBuilder {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AutoRootSignatureBuilder() = default;
		~AutoRootSignatureBuilder() = default;

		// 配置計画を作成し、成功時だけRoot Signatureを返す
		RootSignatureBuildResult Build(ID3D12Device* device, PipelineType pipelineType,
			const std::vector<const CompiledShader*>& shaders,
			const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers = {});
	};
}