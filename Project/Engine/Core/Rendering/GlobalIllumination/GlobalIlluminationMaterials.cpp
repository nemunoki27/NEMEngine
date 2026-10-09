#include "GlobalIlluminationMaterials.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingShaderLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveBatchResources.h>

// c++
#include <algorithm>
#include <cstring>

//============================================================================
//	GlobalIlluminationMaterials classMethods
//============================================================================
Engine::GlobalIlluminationMaterialBinding Engine::GlobalIlluminationMaterials::Resolve(GraphicsCore& graphicsCore,
	AssetDatabase& database, RenderAssetLibrary& library, const MaterialAsset& material,
	const MaterialParameterSet& overrides) {

	if (!material.shaderGraph) return {};
	descriptors_ = &graphicsCore.GetSRVDescriptor();
	auto graph = std::find_if(graphs_.begin(), graphs_.end(), [&](const GraphEntry& entry) {

		return entry.graph == material.shaderGraph;
	});
	if (graph == graphs_.end()) {

		// 既存のDXR定数配置をCallableでも使用
		const auto id = ShaderGraphArtifactCache::MakeDerivedID(material.shaderGraph, 0x47494d415445524cull);
		const auto* shader = library.LoadShader(id);
		const auto* pass = FindPass(material, MaterialPassKind::RayTracing);
		const auto* source = pass ? library.LoadShader(pass->shaderOverride) : nullptr;
		if (!shader || !source || source->stages.empty()) return {};
		auto compiled = RaytracingShaderLibrary::Load(graphicsCore.GetDXObject().GetDxShaderCompiler(), source->stages[0]);
		if (!compiled.IsValid()) return {};
		ApplyShaderParameterMetadata(compiled.reflection, *source);
		GraphEntry entry;
		entry.graph = material.shaderGraph;
		entry.shader = *shader;
		const auto* pipeline = library.LoadPipeline(pass->pipeline);
		if (pipeline && !pipeline->variants.empty()) {

			entry.samplers = pipeline->variants[0].staticSamplers;
			for (auto& sampler : entry.samplers) {

				if (sampler.ShaderRegister >= 2) sampler.RegisterSpace = 16u + static_cast<uint32_t>(material.shaderGraph.low & 0x7fffffffu);
			}
		}
		entry.layout.Build(compiled.reflection, "RayTracingParameters");

		graphs_.push_back(std::move(entry));
		graph = graphs_.end() - 1;
	}
	const uint32_t callable = static_cast<uint32_t>(graph - graphs_.begin());
	const auto resolveTexture = [&](MaterialParameterSemantic semantic, const AssetID& texture) {

		const auto result = RuntimeTextureResolver::ResolveBindless(graphicsCore, &database, texture,
			IsSRGBMaterialTexture(semantic) ? TextureColorSpace::SRGB : TextureColorSpace::Linear,
			semantic == MaterialParameterSemantic::NormalTexture);
		return MaterialParameterBufferBuilder::TextureResolveResult{ result.srvIndex, !result.retry };
	};
	const auto bytes = graph->layout.IsValid() ?
		MaterialParameterBufferBuilder::BuildElement(material.parameters, overrides, graph->layout, resolveTexture) :
		std::vector<uint8_t>(16, 0);
	return { callable, ResolveParameters(graphicsCore, graph->graph, bytes) };
}

uint32_t Engine::GlobalIlluminationMaterials::ResolveParameters(GraphicsCore& graphicsCore, AssetID graph,
	const std::vector<uint8_t>& bytes) {

	descriptors_ = &graphicsCore.GetSRVDescriptor();
	for (const auto& parameter : parameters_) {

		if (parameter->graph == graph && parameter->bytes == bytes) {

			parameter->used = true;
			return parameter->descriptor;
		}
	}
	// 公開済みの定数とDescriptorは上書きしない
	auto parameter = std::make_unique<ParameterEntry>();
	parameter->graph = graph;
	parameter->bytes = bytes;
	const uint32_t size = static_cast<uint32_t>((bytes.size() + 255u) & ~255u);
	auto& platform = graphicsCore.GetDXObject();
	parameter->buffer.Create(platform.GetResourceRetirement(), platform.GetDevice(), size);
	parameter->buffer.Write(bytes.data(), bytes.size());
	parameter->descriptor = descriptors_->Allocate();
	const D3D12_CONSTANT_BUFFER_VIEW_DESC description{ parameter->buffer.GetGPUVirtualAddress(), size };
	platform.GetDevice()->CreateConstantBufferView(&description, descriptors_->GetCPUHandle(parameter->descriptor));
	const uint32_t descriptor = parameter->descriptor;
	parameters_.push_back(std::move(parameter));
	return descriptor;
}

void Engine::GlobalIlluminationMaterials::AppendShaders(ShaderAsset& shader, PipelineVariantDesc& variant,
	PipelineStaticSamplerOverrideSet&) const {

	// Callableの順序をGeometryの番号と一致させる
	for (const auto& graph : graphs_) {

		for (const auto& sampler : graph.samplers) {

			if (sampler.ShaderRegister >= 2u) variant.staticSamplers.push_back(sampler);
		}
		for (const auto& stage : graph.shader.stages) {

			shader.stages.push_back(stage);
			variant.callableExports.push_back(stage.entry);
		}
	}
}

void Engine::GlobalIlluminationMaterials::Clear() {

	// 参照中のDescriptorはGPU完了後に回収
	if (descriptors_) {

		for (auto& parameter : parameters_) descriptors_->Retire(parameter->descriptor, {});
	}
	parameters_.clear();
	graphs_.clear();
	descriptors_ = nullptr;
}

Engine::GlobalIlluminationMaterials::~GlobalIlluminationMaterials() {

	Clear();
}

void Engine::GlobalIlluminationMaterials::BeginBuild() {

	for (auto& entry : parameters_) entry->used = false;
}

void Engine::GlobalIlluminationMaterials::EndBuild() {

	// 再構築後に参照されない定数を回収
	std::erase_if(parameters_, [&](const auto& entry) {

		if (entry->used) return false;
		descriptors_->Retire(entry->descriptor, {});
		return true;
	});
}

uint32_t Engine::GlobalIlluminationMaterials::ResolvePrimitiveColor(GraphicsCore& graphicsCore,
	const PrimitiveRendererComponent& renderer) {

	// 通常描画と同じ形状値から頂点色を求める
	PrimitiveInstanceData instance;
	ApplyPrimitiveShapeToInstance(renderer, instance);
	std::vector<uint8_t> bytes(sizeof(instance));
	std::memcpy(bytes.data(), &instance, sizeof(instance));
	return ResolveParameters(graphicsCore, {}, bytes);
}
