#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleBatchResources.h>

namespace Engine {

	//============================================================================
	//	ParticleRenderDataUtility functions
	//============================================================================

	// PSのStructuredBuffer reflectionから可変パラメータレイアウトを作る
	ParticleCustomParameterLayout BuildParticleCustomParameterLayout(const ShaderReflectionInfo& reflection);
	// 可変パラメータへfloat値を書き込む
	void WriteParticleCustomParameter(std::vector<uint8_t>& data,
		const ShaderConstantBufferVariable& variable, const Vector4& value);
} // Engine
