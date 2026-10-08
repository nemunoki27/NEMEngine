#include "ShaderGraphArtifactBuilder.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphArtifactCache.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <span>

namespace {

	// Shaderの段階と公開Parameterを構成する
	Engine::ShaderAsset MakeShader(std::string_view name, Engine::AssetID shaderID, const std::filesystem::path& path,
		Engine::ShaderStage stage, std::string_view profile, std::span<const std::string_view> entries,
		const std::vector<Engine::ShaderParameterMetadata>& parameters) {

		// 識別子と段階を設定する
		Engine::ShaderAsset shader{};
		shader.guid = shaderID;
		shader.name = std::string(name);
		for (std::string_view entry : entries) {
			shader.stages.emplace_back(Engine::ShaderStageEntry{
				.stage = stage,
				.file = Engine::Algorithm::PathToUTF8(path),
				.entry = std::string(entry),
				.profile = std::string(profile),
			});
		}
		// 公開値と色の編集属性を引き継ぐ
		shader.parameters = parameters;
		for (const auto& parameter : parameters) {
			if (parameter.isColor) {
				shader.colorParameters.emplace_back(parameter.shaderName);
			}
		}
		return shader;
	}

	// 描画対象に応じた基底Pipelineを選ぶ
	Engine::AssetID ResolveBasePipeline(Engine::ShaderGraphTarget target, bool transparent) {

		using namespace Engine;
		switch (target) {
		case ShaderGraphTarget::Mesh:
			return transparent ? BuiltinAssets::Pipelines::DefaultMeshTransparent : BuiltinAssets::Pipelines::DefaultMesh;
		case ShaderGraphTarget::Primitive3D:
			return transparent ? BuiltinAssets::Pipelines::DefaultPrimitiveTransparent
							   : BuiltinAssets::Pipelines::DefaultPrimitive;
		case ShaderGraphTarget::Sprite:
			return BuiltinAssets::Pipelines::DefaultSprite;
		case ShaderGraphTarget::Text:
			return BuiltinAssets::Pipelines::DefaultText;
		case ShaderGraphTarget::Primitive2D:
			return BuiltinAssets::Pipelines::DefaultPrimitive2D;
		case ShaderGraphTarget::Particle:
			return BuiltinAssets::Pipelines::DefaultParticle;
		case ShaderGraphTarget::Trail:
			return BuiltinAssets::Pipelines::ParticleTrail;
		}
		return {};
	}

	// Sampler設定をGPU記述へ変換する
	D3D12_STATIC_SAMPLER_DESC MakeGraphSampler(const Engine::PipelineStaticSamplerSettings& settings, uint32_t shaderRegister) {

		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter = settings.filter;
		sampler.AddressU = settings.addressU;
		sampler.AddressV = settings.addressV;
		sampler.AddressW = settings.addressW;
		sampler.MipLODBias = settings.mipLODBias;
		sampler.MaxAnisotropy = (std::clamp)(settings.maxAnisotropy, 1u, 16u);
		sampler.ComparisonFunc = settings.comparisonFunc;
		sampler.BorderColor = settings.borderColor;
		sampler.MinLOD = settings.minLOD;
		sampler.MaxLOD = settings.maxLOD;
		sampler.ShaderRegister = shaderRegister;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		return sampler;
	}
}

namespace Engine::ShaderGraphArtifactBuilder {

	// Pixel Shaderの構成を作成する
	Engine::ShaderAsset MakePixelShader(std::string_view name, Engine::AssetID shaderID, const std::filesystem::path& path,
		std::string_view entry, const std::vector<Engine::ShaderParameterMetadata>& parameters) {

		// 指定のPixel入口を登録する
		const std::string_view entries[] = {entry};
		return MakeShader(name, shaderID, path, ShaderStage::PS, "ps_6_0", entries, parameters);
	}

	// Compute Shaderの構成を作成する
	Engine::ShaderAsset MakeComputeShader(std::string_view name, Engine::AssetID shaderID, const std::filesystem::path& path,
		const std::vector<Engine::ShaderParameterMetadata>& parameters) {

		// Computeの入口を登録する
		constexpr std::string_view entries[] = {"main"};
		return MakeShader(name, shaderID, path, ShaderStage::CS, "cs_6_0", entries, parameters);
	}

	// RayTracing Shaderの構成を作成する
	Engine::ShaderAsset MakeRayTracingShader(std::string_view name, Engine::AssetID shaderID, const std::filesystem::path& path,
		const std::vector<Engine::ShaderParameterMetadata>& parameters, bool renderFeature) {

		// 実行領域に合わせてExport順を固定する
		constexpr std::string_view effectEntries[] = {"RenderFeatureRayGeneration", "ReflectionMiss", "ReflectionClosestHit"};
		constexpr std::string_view surfaceEntries[] = {"ReflectionClosestHit", "ReflectionAnyHit"};
		const std::span<const std::string_view> entries = renderFeature ? std::span<const std::string_view>(effectEntries)
																		: std::span<const std::string_view>(surfaceEntries);
		return MakeShader(name, shaderID, path, ShaderStage::Lib, "lib_6_6", entries, parameters);
	}

	// 基底PipelineへGraph設定を反映する
	bool MakeGraphPipeline(const Engine::ShaderGraphAsset& graph, const Engine::ShaderGraphCompileOutput& compileOutput,
		Engine::AssetID graphID, bool transparent, Engine::AssetDatabase* database, Engine::RenderPipelineAsset& outPipeline,
		Engine::AssetID& outPipelineID, Engine::AssetID baseOverride, uint64_t derivedDiscriminator,
		std::string_view nameSuffix) {

		if (!database) {
			return false;
		}
		const Engine::AssetID baseID = baseOverride ? baseOverride : ResolveBasePipeline(graph.target, transparent);
		const std::filesystem::path path = database->ResolveFullPath(baseID);
		if (!baseID || path.empty() || !Engine::FromJson(Engine::JsonAdapter::Load(path, true), outPipeline)) {
			return false;
		}
		const uint64_t discriminator =
			derivedDiscriminator != 0
				? derivedDiscriminator
				: (transparent ? 0x5452414e535f504cull : 0x4f50415155455f4cull) ^ static_cast<uint64_t>(graph.target);
		outPipelineID = Engine::ShaderGraphArtifactCache::MakeDerivedID(graphID, discriminator);
		outPipeline.guid = outPipelineID;
		outPipeline.name = graph.name + (nameSuffix.empty() ? (transparent ? "TransparentPipeline" : "OpaquePipeline")
															: std::string(nameSuffix));
		// 各Variantへ描画設定とSamplerを反映する
		for (Engine::PipelineVariantDesc& variant : outPipeline.variants) {
			variant.rasterizer.FillMode = graph.renderState.fillMode;
			variant.rasterizer.CullMode = graph.renderState.twoSided ? D3D12_CULL_MODE_NONE : graph.renderState.cullMode;
			variant.rasterizer.FrontCounterClockwise = graph.renderState.frontCounterClockwise;
			variant.rasterizer.DepthClipEnable = graph.renderState.depthClipEnable;
			variant.depthStencil.DepthEnable = graph.renderState.depthTest;
			variant.depthStencil.DepthWriteMask =
				graph.renderState.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
			variant.depthStencil.DepthFunc = graph.renderState.depthFunc;
			variant.depthStencil.StencilEnable = graph.renderState.stencilEnable;
			Engine::PipelineStaticSamplerSettings defaultSampler{};
			defaultSampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			defaultSampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			defaultSampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			const auto addOrReplaceSampler = [&variant](const D3D12_STATIC_SAMPLER_DESC& sampler) {
				const auto found = std::find_if(variant.staticSamplers.begin(), variant.staticSamplers.end(),
					[&sampler](const D3D12_STATIC_SAMPLER_DESC& current) {
						return current.ShaderRegister == sampler.ShaderRegister &&
							   current.RegisterSpace == sampler.RegisterSpace;
					});
				if (found != variant.staticSamplers.end()) {
					*found = sampler;
				} else {
					variant.staticSamplers.emplace_back(sampler);
				}
			};
			addOrReplaceSampler(MakeGraphSampler(defaultSampler, 0));
			for (const Engine::ShaderGraphSamplerBinding& sampler : compileOutput.samplers) {
				addOrReplaceSampler(MakeGraphSampler(sampler.settings, sampler.shaderRegister));
			}
		}
		return true;
	}

	// 反射PipelineへGraph設定を反映する
	bool MakeRayTracingPipeline(const Engine::ShaderGraphAsset& graph, const Engine::ShaderGraphCompileOutput& compileOutput,
		Engine::AssetID graphID, Engine::AssetDatabase* database, Engine::RenderPipelineAsset& outPipeline,
		Engine::AssetID& outPipelineID) {

		if (!database) {
			return false;
		}
		const std::filesystem::path path = database->ResolveFullPath(Engine::BuiltinAssets::Pipelines::RaytracingReflection);
		if (path.empty() || !Engine::FromJson(Engine::JsonAdapter::Load(path, true), outPipeline)) {

			return false;
		}
		outPipelineID = Engine::ShaderGraphArtifactCache::MakeDerivedID(graphID, 0x5241595452414350ull);
		outPipeline.guid = outPipelineID;
		outPipeline.name = graph.name + "RayTracingPipeline";
		// 各Variantへ描画設定とSamplerを反映する
		for (Engine::PipelineVariantDesc& variant : outPipeline.variants) {
			if (variant.kind != Engine::PipelineVariantKind::Raytracing) {
				continue;
			}
			if (graph.domain == Engine::ShaderGraphDomain::RayTracingEffect) {
				variant.rayGenerationExports = {"RenderFeatureRayGeneration"};
			} else {
				for (Engine::RaytracingHitGroupDesc& hitGroup : variant.hitGroups) {

					if (hitGroup.closestHitExport == "ReflectionClosestHit") {
						hitGroup.anyHitExport = "ReflectionAnyHit";
					}
				}
			}
			for (const Engine::ShaderGraphSamplerBinding& sampler : compileOutput.samplers) {

				const D3D12_STATIC_SAMPLER_DESC graphSampler = MakeGraphSampler(sampler.settings, sampler.shaderRegister);
				const auto found = std::find_if(variant.staticSamplers.begin(), variant.staticSamplers.end(),
					[&graphSampler](const D3D12_STATIC_SAMPLER_DESC& current) {
						return current.ShaderRegister == graphSampler.ShaderRegister &&
							   current.RegisterSpace == graphSampler.RegisterSpace;
					});
				if (found != variant.staticSamplers.end()) {
					*found = graphSampler;
				} else {
					variant.staticSamplers.emplace_back(graphSampler);
				}
			}
		}
		return true;
	}
}
