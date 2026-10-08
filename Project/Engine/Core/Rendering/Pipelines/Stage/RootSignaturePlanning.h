#pragma once

//============================================================================
//	include
//============================================================================
#include "RootSignatureLayout.h"

namespace Engine::RootSignaturePlanning {

	// Shaderの読み取り結果からルート配置を確定する
	RootSignaturePlan Build(PipelineType pipelineType, const std::vector<const CompiledShader*>& shaders,
		const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers);
}
