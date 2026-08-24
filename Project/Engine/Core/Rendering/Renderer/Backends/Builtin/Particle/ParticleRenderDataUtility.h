#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleBatchResources.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>

namespace Engine {
	class MaterialParameterSet;
	struct ParticleGroupRuntimeState;
	struct ParticleRenderPayload;
	struct RenderItem;

	//============================================================================
	//	ParticleRenderDataUtility functions
	//============================================================================

	// PSのStructuredBuffer reflectionから可変パラメータレイアウトを作る
	ParticleCustomParameterLayout BuildParticleCustomParameterLayout(
		const ShaderReflectionInfo& reflection, const MaterialParameterSet* defaults,
		const MaterialParameterBufferBuilder::TextureResolver& resolveTexture);
	// 可変パラメータへモジュールの数値を書き込む
	void WriteParticleCustomParameter(std::vector<uint8_t>& data,
		const ShaderConstantBufferVariable& variable, const Vector4& value);
	// RenderItemから現在のParticleグループを解決する
	const ParticleGroupRuntimeState* ResolveParticleRenderGroup(
		const RenderItem& item, const ParticleRenderPayload& payload);
} // Engine
