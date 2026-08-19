#include "ShaderGraphArtifactCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <fstream>

namespace {

	bool WriteTextFile(const std::filesystem::path& path,
		std::string_view source) {

		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ec) {
			return false;
		}
		std::ofstream stream(path,
			std::ios::binary | std::ios::trunc);
		if (!stream.is_open()) {
			return false;
		}
		stream.write(source.data(),
			static_cast<std::streamsize>(source.size()));
		return stream.good();
	}

	Engine::ShaderAsset MakePixelShader(
		std::string_view name,
		Engine::AssetID shaderID,
		const std::filesystem::path& path,
		std::string_view entry,
		const std::vector<Engine::ShaderParameterMetadata>& parameters) {

		Engine::ShaderAsset shader{};
		shader.guid = shaderID;
		shader.name = std::string(name);
		shader.stages.emplace_back(Engine::ShaderStageEntry{
			.stage = Engine::ShaderStage::PS,
			.file = Engine::Algorithm::PathToUTF8(path),
			.entry = std::string(entry),
			.profile = "ps_6_6",
			});
		shader.parameters = parameters;
		for (const Engine::ShaderParameterMetadata& parameter : parameters) {
			if (parameter.isColor) {
				shader.colorParameters.emplace_back(parameter.shaderName);
			}
		}
		return shader;
	}

	Engine::ShaderAsset MakeComputeShader(
		std::string_view name,
		Engine::AssetID shaderID,
		const std::filesystem::path& path,
		const std::vector<Engine::ShaderParameterMetadata>& parameters) {

		Engine::ShaderAsset shader{};
		shader.guid = shaderID;
		shader.name = std::string(name);
		shader.stages.emplace_back(Engine::ShaderStageEntry{
			.stage = Engine::ShaderStage::CS,
			.file = Engine::Algorithm::PathToUTF8(path),
			.entry = "main",
			.profile = "cs_6_6",
			});
		shader.parameters = parameters;
		for (const Engine::ShaderParameterMetadata& parameter : parameters) {
			if (parameter.isColor) {
				shader.colorParameters.emplace_back(parameter.shaderName);
			}
		}
		return shader;
	}

	Engine::ShaderAsset MakeRayTracingShader(
		std::string_view name,
		Engine::AssetID shaderID,
		const std::filesystem::path& path,
		const std::vector<Engine::ShaderParameterMetadata>& parameters,
		bool renderFeature) {

		Engine::ShaderAsset shader{};
		shader.guid = shaderID;
		shader.name = std::string(name);
		const std::vector<const char*> entries = renderFeature ?
			std::vector<const char*>{ "RenderFeatureRayGeneration",
				"ReflectionMiss", "ReflectionClosestHit" } :
			std::vector<const char*>{ "ReflectionClosestHit",
				"ReflectionAnyHit" };
		for (const char* entry : entries) {
			shader.stages.emplace_back(Engine::ShaderStageEntry{
				.stage = Engine::ShaderStage::Lib,
				.file = Engine::Algorithm::PathToUTF8(path),
				.entry = entry,
				.profile = "lib_6_6",
				});
		}
		shader.parameters = parameters;
		for (const Engine::ShaderParameterMetadata& parameter : parameters) {
			if (parameter.isColor) {
				shader.colorParameters.emplace_back(parameter.shaderName);
			}
		}
		return shader;
	}

	Engine::AssetID ResolveBasePipeline(
		Engine::ShaderGraphTarget target, bool transparent) {

		using namespace Engine;
		switch (target) {
		case ShaderGraphTarget::Mesh:
			return transparent ? BuiltinAssets::Pipelines::DefaultMeshTransparent :
				BuiltinAssets::Pipelines::DefaultMesh;
		case ShaderGraphTarget::Primitive3D:
			return transparent ? BuiltinAssets::Pipelines::DefaultPrimitiveTransparent :
				BuiltinAssets::Pipelines::DefaultPrimitive;
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

	D3D12_STATIC_SAMPLER_DESC MakeGraphSampler(
		const Engine::PipelineStaticSamplerSettings& settings,
		uint32_t shaderRegister) {

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

	bool MakeGraphPipeline(const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphCompileOutput& compileOutput,
		Engine::AssetID graphID, bool transparent,
		Engine::AssetDatabase* database,
		Engine::RenderPipelineAsset& outPipeline,
		Engine::AssetID& outPipelineID,
		Engine::AssetID baseOverride = {},
		uint64_t derivedDiscriminator = 0,
		std::string_view nameSuffix = {}) {

		if (!database) {
			return false;
		}
		const Engine::AssetID baseID = baseOverride ? baseOverride :
			ResolveBasePipeline(graph.target, transparent);
		const std::filesystem::path path =
			database->ResolveFullPath(baseID);
		if (!baseID || path.empty() ||
			!Engine::FromJson(Engine::JsonAdapter::Load(path, true), outPipeline)) {
			return false;
		}
		const uint64_t discriminator = derivedDiscriminator != 0 ?
			derivedDiscriminator :
			(transparent ? 0x5452414e535f504cull :
				0x4f50415155455f4cull) ^ static_cast<uint64_t>(graph.target);
		outPipelineID = Engine::ShaderGraphArtifactCache::MakeDerivedID(
			graphID, discriminator);
		outPipeline.guid = outPipelineID;
		outPipeline.name = graph.name +
			(nameSuffix.empty() ?
				(transparent ? "TransparentPipeline" : "OpaquePipeline") :
				std::string(nameSuffix));
		for (Engine::PipelineVariantDesc& variant : outPipeline.variants) {
			variant.rasterizer.FillMode = graph.renderState.fillMode;
			variant.rasterizer.CullMode = graph.renderState.twoSided ?
				D3D12_CULL_MODE_NONE : graph.renderState.cullMode;
			variant.rasterizer.FrontCounterClockwise =
				graph.renderState.frontCounterClockwise;
			variant.rasterizer.DepthClipEnable =
				graph.renderState.depthClipEnable;
			variant.depthStencil.DepthEnable = graph.renderState.depthTest;
			variant.depthStencil.DepthWriteMask = graph.renderState.depthWrite ?
				D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
			variant.depthStencil.DepthFunc = graph.renderState.depthFunc;
			variant.depthStencil.StencilEnable = graph.renderState.stencilEnable;
			Engine::PipelineStaticSamplerSettings defaultSampler{};
			defaultSampler.addressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			defaultSampler.addressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			defaultSampler.addressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
			const auto addOrReplaceSampler =
				[&variant](const D3D12_STATIC_SAMPLER_DESC& sampler) {
					const auto found = std::find_if(
						variant.staticSamplers.begin(),
						variant.staticSamplers.end(),
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
			for (const Engine::ShaderGraphSamplerBinding& sampler :
				compileOutput.samplers) {
				addOrReplaceSampler(MakeGraphSampler(
					sampler.settings, sampler.shaderRegister));
			}
		}
		return true;
	}

	bool MakeRayTracingPipeline(
		const Engine::ShaderGraphAsset& graph,
		const Engine::ShaderGraphCompileOutput& compileOutput,
		Engine::AssetID graphID, Engine::AssetDatabase* database,
		Engine::RenderPipelineAsset& outPipeline,
		Engine::AssetID& outPipelineID) {

		if (!database) {
			return false;
		}
		const std::filesystem::path path = database->ResolveFullPath(
			Engine::BuiltinAssets::Pipelines::RaytracingReflection);
		if (path.empty() || !Engine::FromJson(
			Engine::JsonAdapter::Load(path, true), outPipeline)) {

			return false;
		}
		outPipelineID = Engine::ShaderGraphArtifactCache::MakeDerivedID(
			graphID, 0x5241595452414350ull);
		outPipeline.guid = outPipelineID;
		outPipeline.name = graph.name + "RayTracingPipeline";
		for (Engine::PipelineVariantDesc& variant : outPipeline.variants) {
			if (variant.kind != Engine::PipelineVariantKind::Raytracing) {
				continue;
			}
			if (graph.domain == Engine::ShaderGraphDomain::RayTracingEffect) {
				variant.rayGenerationExports = {
					"RenderFeatureRayGeneration" };
			} else {
				for (Engine::RaytracingHitGroupDesc& hitGroup :
					variant.hitGroups) {

					if (hitGroup.closestHitExport == "ReflectionClosestHit") {
						hitGroup.anyHitExport = "ReflectionAnyHit";
					}
				}
			}
			for (const Engine::ShaderGraphSamplerBinding& sampler :
				compileOutput.samplers) {

				const D3D12_STATIC_SAMPLER_DESC graphSampler =
					MakeGraphSampler(sampler.settings, sampler.shaderRegister);
				const auto found = std::find_if(
					variant.staticSamplers.begin(), variant.staticSamplers.end(),
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

//============================================================================
//	ShaderGraphArtifactCache classMethods
//============================================================================
bool Engine::ShaderGraphArtifactCache::Compile(
	const ShaderGraphAsset& graph, AssetID graphID,
	ShaderGraphArtifact& outArtifact,
	AssetDatabase* database) {

	outArtifact = ShaderGraphArtifact{};
	if (!graphID) {
		return false;
	}

	const std::string graphIDText = ToString(graphID);
	const std::string targetName = graph.domain == ShaderGraphDomain::PostProcess ?
		"PostProcess" :
		std::string(EnumAdapter<ShaderGraphTarget>::ToString(graph.target));
	outArtifact.root = RuntimePaths::GetLibraryPath(
		Algorithm::PathFromUTF8(
			"ShaderGraph/" + graphIDText + "/" + targetName));
	outArtifact.surfacePath = outArtifact.root / "surface.generated.hlsli";
	outArtifact.opaquePixelPath = outArtifact.root / "opaque.PS.hlsl";
	outArtifact.transparentPixelPath = outArtifact.root / "transparent.PS.hlsl";
	outArtifact.depthPixelPath = outArtifact.root / "depth.PS.hlsl";
	outArtifact.pickingPixelPath = outArtifact.root / "picking.PS.hlsl";
	outArtifact.vertexPath = outArtifact.root / "vertex.VS.hlsl";
	outArtifact.meshPath = outArtifact.root / "mesh.MS.hlsl";
	outArtifact.computePath = outArtifact.root / "postProcess.CS.hlsl";
	outArtifact.rayTracingPath = outArtifact.root / "rayTracing.RT.hlsl";

	const ShaderGraphAssetResolver resolver =
		[database](AssetID assetID, ShaderGraphAsset& outGraph) {
			if (!database) {
				return false;
			}
			const std::filesystem::path path =
				database->ResolveFullPath(assetID);
			return !path.empty() &&
				FromJson(JsonAdapter::Load(path, true), outGraph);
		};
	ShaderGraphAsset resolvedGraph = graph;
	if (database) {
		for (ShaderGraphNode& node : resolvedGraph.nodes) {
			if (node.customFunctionSource !=
				ShaderGraphCustomFunctionSource::File ||
				!node.functionFileAsset) {

				continue;
			}
			const std::filesystem::path functionPath =
				database->ResolveFullPath(node.functionFileAsset);
			node.functionFile = Algorithm::PathToUTF8(
				functionPath.lexically_normal());
			std::replace(node.functionFile.begin(),
				node.functionFile.end(), '\\', '/');
		}
	}
	outArtifact.compileOutput = ShaderGraphCompiler::Compile(
		resolvedGraph,
		Algorithm::PathToUTF8(outArtifact.surfacePath.filename()),
		resolver);
	if (!outArtifact.compileOutput.Succeeded()) {
		for (const ShaderGraphDiagnostic& diagnostic :
			outArtifact.compileOutput.diagnostics) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[ShaderGraph] graph={} node={} stage={} message={}",
				ToString(graphID), ToString(diagnostic.node),
				EnumAdapter<ShaderGraphStage>::ToString(diagnostic.stage),
				diagnostic.message);
		}
		return false;
	}
	if (graph.domain == ShaderGraphDomain::PostProcess) {
		if (!WriteTextFile(outArtifact.computePath,
			outArtifact.compileOutput.computeHLSL)) {
			return false;
		}
		outArtifact.computeShaderID = MakeDerivedID(
			graphID, 0x504f535450524f43ull);
		outArtifact.computeShader = MakeComputeShader(
			graph.name + "Compute", outArtifact.computeShaderID,
			outArtifact.computePath,
			outArtifact.compileOutput.parameters);
		MakeGraphPipeline(graph, outArtifact.compileOutput,
			graphID, false, database,
			outArtifact.computePipeline,
			outArtifact.computePipelineID,
			BuiltinAssets::Pipelines::PostProcessMaskComposite,
			0x504f535450495045ull, "ComputePipeline");
		Logger::Output(LogType::Engine,
			"[ShaderGraph] compiled graph={} target=PostProcess shader={}",
			ToString(graphID), ToString(outArtifact.computeShaderID));
		return true;
	}
	if (graph.domain == ShaderGraphDomain::RayTracingEffect) {
		if (outArtifact.compileOutput.rayTracingHLSL.empty() ||
			!WriteTextFile(outArtifact.rayTracingPath,
				outArtifact.compileOutput.rayTracingHLSL)) {

			return false;
		}
		outArtifact.rayTracingShaderID = MakeDerivedID(
			graphID, 0x5241594645415455ull);
		outArtifact.rayTracingShader = MakeRayTracingShader(
			graph.name + "RayTracingFeature",
			outArtifact.rayTracingShaderID,
			outArtifact.rayTracingPath,
			outArtifact.compileOutput.parameters, true);
		if (!MakeRayTracingPipeline(graph, outArtifact.compileOutput,
			graphID, database, outArtifact.rayTracingPipeline,
			outArtifact.rayTracingPipelineID)) {

			return false;
		}
		Logger::Output(LogType::Engine,
			"[ShaderGraph] compiled graph={} target=RayTracingFeature shader={}",
			ToString(graphID), ToString(outArtifact.rayTracingShaderID));
		return true;
	}
	if (!WriteTextFile(outArtifact.surfacePath,
		outArtifact.compileOutput.surfaceHLSL) ||
		!WriteTextFile(outArtifact.opaquePixelPath,
			outArtifact.compileOutput.opaquePixelHLSL) ||
		!WriteTextFile(outArtifact.transparentPixelPath,
			outArtifact.compileOutput.transparentPixelHLSL)) {

		return false;
	}
	if (!outArtifact.compileOutput.rayTracingHLSL.empty()) {
		if (!WriteTextFile(outArtifact.rayTracingPath,
			outArtifact.compileOutput.rayTracingHLSL)) {

			return false;
		}
		outArtifact.rayTracingShaderID = MakeDerivedID(
			graphID, 0x5241595452414345ull ^
				static_cast<uint64_t>(graph.target));
		outArtifact.rayTracingShader = MakeRayTracingShader(
			graph.name + "RayTracing", outArtifact.rayTracingShaderID,
			outArtifact.rayTracingPath,
			outArtifact.compileOutput.parameters, false);
		if (!MakeRayTracingPipeline(graph, outArtifact.compileOutput,
			graphID, database, outArtifact.rayTracingPipeline,
			outArtifact.rayTracingPipelineID)) {

			return false;
		}
	}

	outArtifact.opaqueShaderID = MakeDerivedID(
		graphID, 0x4f50415155455f50ull ^
			static_cast<uint64_t>(graph.target));
	outArtifact.transparentShaderID = MakeDerivedID(
		graphID, 0x5452414e535f5053ull ^
			static_cast<uint64_t>(graph.target));
	outArtifact.opaqueShader = MakePixelShader(
		graph.name + "Opaque", outArtifact.opaqueShaderID,
		outArtifact.opaquePixelPath, "main",
		outArtifact.compileOutput.parameters);
	outArtifact.transparentShader = MakePixelShader(
		graph.name + "Transparent", outArtifact.transparentShaderID,
		outArtifact.transparentPixelPath,
		IsShaderGraph3DTarget(graph.target) ? "mainTransparent" : "main",
		outArtifact.compileOutput.parameters);
	const bool hasVertexGraph =
		!outArtifact.compileOutput.vertexHLSL.empty();
	const bool hasMeshGraph =
		!outArtifact.compileOutput.meshHLSL.empty();
	if (hasVertexGraph &&
		(!WriteTextFile(outArtifact.vertexPath,
			outArtifact.compileOutput.vertexHLSL) ||
		 (hasMeshGraph &&
			!WriteTextFile(outArtifact.meshPath,
				outArtifact.compileOutput.meshHLSL)))) {

		return false;
	}
	const auto appendGeneratedGeometryStages =
		[&](ShaderAsset& shader) {

		shader.stages.emplace_back(ShaderStageEntry{
			.stage = ShaderStage::VS,
			.file = Algorithm::PathToUTF8(
				outArtifact.vertexPath),
			.entry = "main",
			.profile = "vs_6_6",
		});
		if (hasMeshGraph) {
			shader.stages.emplace_back(ShaderStageEntry{
				.stage = ShaderStage::MS,
				.file = Algorithm::PathToUTF8(
					outArtifact.meshPath),
				.entry = "main",
				.profile = "ms_6_6",
			});
		}
	};
	if (graph.target == ShaderGraphTarget::Mesh) {
		if (!WriteTextFile(outArtifact.depthPixelPath,
			outArtifact.compileOutput.depthPixelHLSL) ||
			!WriteTextFile(outArtifact.pickingPixelPath,
				outArtifact.compileOutput.pickingPixelHLSL)) {

			return false;
		}
		outArtifact.depthShaderID = MakeDerivedID(
			graphID, 0x44455054485f5053ull);
		outArtifact.pickingShaderID = MakeDerivedID(
			graphID, 0x5049434b494e4750ull);
		outArtifact.depthShader = MakePixelShader(
			graph.name + "Depth", outArtifact.depthShaderID,
			outArtifact.depthPixelPath, "main",
			outArtifact.compileOutput.parameters);
		outArtifact.pickingShader = MakePixelShader(
			graph.name + "Picking", outArtifact.pickingShaderID,
			outArtifact.pickingPixelPath, "main",
			outArtifact.compileOutput.parameters);

		const std::filesystem::path vertexPath = hasVertexGraph ?
			outArtifact.vertexPath : RuntimePaths::GetEngineAssetPath(
				"Shaders/Builtin/Mesh/Common/meshGeometry.VS.hlsl");
		const std::filesystem::path meshPath = hasVertexGraph ?
			outArtifact.meshPath : RuntimePaths::GetEngineAssetPath(
				"Shaders/Builtin/Mesh/Common/meshGeometry.MS.hlsl");
		const auto appendGeometryStages = [&](ShaderAsset& shader) {

			shader.stages.emplace_back(ShaderStageEntry{
				.stage = ShaderStage::VS,
				.file = Algorithm::PathToUTF8(vertexPath),
				.entry = "main",
				.profile = hasVertexGraph ? "vs_6_6" : "vs_6_0",
			});
			shader.stages.emplace_back(ShaderStageEntry{
				.stage = ShaderStage::MS,
				.file = Algorithm::PathToUTF8(meshPath),
				.entry = "main",
				.profile = "ms_6_6",
			});
		};
		// Alpha Clip評価にはUV等が必要なため、深度専用Geometryを通常Geometryへ差し替える
		appendGeometryStages(outArtifact.depthShader);
		if (hasVertexGraph) {
			appendGeometryStages(outArtifact.opaqueShader);
			appendGeometryStages(outArtifact.transparentShader);
			outArtifact.pickingShader.stages.emplace_back(ShaderStageEntry{
				.stage = ShaderStage::VS,
				.file = Algorithm::PathToUTF8(vertexPath),
				.entry = "main",
				.profile = "vs_6_6",
			});
		}
	} else if (hasVertexGraph &&
		(graph.target == ShaderGraphTarget::Primitive3D ||
		 graph.target == ShaderGraphTarget::Primitive2D)) {

		appendGeneratedGeometryStages(outArtifact.opaqueShader);
		appendGeneratedGeometryStages(outArtifact.transparentShader);
	}
	MakeGraphPipeline(graph, outArtifact.compileOutput,
		graphID, false, database,
		outArtifact.opaquePipeline, outArtifact.opaquePipelineID);
	MakeGraphPipeline(graph, outArtifact.compileOutput,
		graphID, true, database,
		outArtifact.transparentPipeline, outArtifact.transparentPipelineID);
	if (graph.target == ShaderGraphTarget::Mesh) {
		MakeGraphPipeline(graph, outArtifact.compileOutput,
			graphID, false, database,
			outArtifact.depthPipeline, outArtifact.depthPipelineID,
			BuiltinAssets::Pipelines::DefaultMeshZPrepass,
			0x44455054485f504cull, "DepthPipeline");
		MakeGraphPipeline(graph, outArtifact.compileOutput,
			graphID, false, database,
			outArtifact.pickingPipeline, outArtifact.pickingPipelineID,
			BuiltinAssets::Pipelines::DefaultMeshEditorPicking,
			0x5049434b494e4750ull, "PickingPipeline");
	}
	Logger::Output(LogType::Engine,
		"[ShaderGraph] compiled graph={} target={} opaqueShader={} transparentShader={}",
		ToString(graphID), EnumAdapter<ShaderGraphTarget>::ToString(graph.target),
		ToString(outArtifact.opaqueShaderID),
		ToString(outArtifact.transparentShaderID));
	return true;
}

Engine::MaterialAsset Engine::ShaderGraphArtifactCache::CreateMaterial(
	const ShaderGraphAsset& graph, AssetID graphID) {

	MaterialAsset material{};
	if (graph.domain == ShaderGraphDomain::RayTracingEffect) {
		material.name = graph.name.empty() ?
			"NewRayTracingFeatureMaterial" : graph.name;
		material.domain = MaterialDomain::RayTracing;
		material.usage = MaterialUsage::Generic;
		material.passes.emplace_back(MaterialPassBinding{
			.passKind = MaterialPassKind::RayTracing,
			.pipeline = BuiltinAssets::Pipelines::RaytracingReflection,
			.preferredVariant = PipelineVariantKind::Raytracing,
		});
	} else if (graph.domain == ShaderGraphDomain::PostProcess) {
		material.name = graph.name.empty() ?
			"NewPostProcessMaterial" : graph.name;
		material.domain = MaterialDomain::Compute;
		material.usage = MaterialUsage::Generic;
		material.passes.emplace_back(MaterialPassBinding{
			.passKind = MaterialPassKind::PostProcess,
			.pipeline = BuiltinAssets::Pipelines::PostProcessMaskComposite,
			.preferredVariant = PipelineVariantKind::Compute,
			});
	} else if (graph.target == ShaderGraphTarget::Mesh) {
		material = CreateDefaultMeshMaterialAsset(graph.name);
	} else {
		material.name = graph.name.empty() ?
			"NewMaterial" : graph.name;
		material.domain =
			graph.target == ShaderGraphTarget::Sprite ||
			graph.target == ShaderGraphTarget::Text ||
			graph.target == ShaderGraphTarget::Primitive2D ?
			MaterialDomain::UI : MaterialDomain::Surface;
		material.usage =
			graph.target == ShaderGraphTarget::Sprite ? MaterialUsage::Sprite :
			(graph.target == ShaderGraphTarget::Text ? MaterialUsage::Text :
				((graph.target == ShaderGraphTarget::Particle ||
					graph.target == ShaderGraphTarget::Trail) ?
					MaterialUsage::Particle : MaterialUsage::Generic));

		const auto addPass = [&](MaterialPassKind passKind,
			AssetID pipeline, PipelineVariantKind variant) {

			material.passes.emplace_back(MaterialPassBinding{
				.passKind = passKind,
				.pipeline = pipeline,
				.preferredVariant = variant,
				});
		};
		switch (graph.target) {
		case ShaderGraphTarget::Primitive3D:
			addPass(MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultPrimitive,
				PipelineVariantKind::GraphicsMesh);
			addPass(MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::DefaultPrimitiveTransparent,
				PipelineVariantKind::GraphicsMesh);
			break;
		case ShaderGraphTarget::Sprite:
			addPass(MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultSprite,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Text:
			addPass(MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultText,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Primitive2D:
			addPass(MaterialPassKind::Draw,
				BuiltinAssets::Pipelines::DefaultPrimitive2D,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Particle:
			addPass(MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::DefaultParticle,
				PipelineVariantKind::GraphicsVertex);
			break;
		case ShaderGraphTarget::Trail:
			addPass(MaterialPassKind::Transparent,
				BuiltinAssets::Pipelines::ParticleTrail,
				PipelineVariantKind::GraphicsMesh);
			break;
		case ShaderGraphTarget::Mesh:
			break;
		}
	}

	material.shaderGraph = graphID;
	if (graph.domain == ShaderGraphDomain::Surface) {
		if (IsShaderGraph3DTarget(graph.target) &&
			!FindPass(material, MaterialPassKind::RayTracing)) {

			material.passes.emplace_back(MaterialPassBinding{
				.passKind = MaterialPassKind::RayTracing,
				.pipeline = BuiltinAssets::Pipelines::RaytracingReflection,
				.preferredVariant = PipelineVariantKind::Raytracing,
				});
		}
		material.renderState.overridesRenderer = true;
		material.renderState.surfaceMode =
			graph.surfaceMode == ShaderGraphSurfaceMode::Transparent ?
			MaterialSurfaceMode::Transparent :
			(graph.renderState.alphaClipping ?
				MaterialSurfaceMode::Masked :
				MaterialSurfaceMode::Opaque);
		material.renderState.phase =
			graph.surfaceMode == ShaderGraphSurfaceMode::Transparent ?
			RenderPhase::Transparent : RenderPhase::Opaque;
		material.renderState.blendMode = graph.renderState.blendMode;
		material.renderState.castShadows = graph.renderState.castShadows;
		material.renderState.receiveShadows = graph.renderState.receiveShadows;
	}
	for (const ShaderGraphParameter& parameter : graph.parameters) {
		if (!parameter.exposed ||
			parameter.scope == ShaderGraphParameterScope::Global) {
			continue;
		}
		material.parameters.Set(
			MaterialParameterID::FromUUID(parameter.id),
			parameter.name, parameter.semantic,
			parameter.defaultValue);
	}
	for (const ShaderGraphKeyword& keyword : graph.keywords) {
		if (!keyword.runtimeToggle) {
			continue;
		}
		MaterialParameterValue value{};
		if (keyword.type == ShaderGraphKeywordType::Boolean) {
			value.value = keyword.defaultIndex != 0;
		} else {
			value.value = static_cast<int32_t>(keyword.defaultIndex);
		}
		material.parameters.Set(
			MaterialParameterID::FromUUID(keyword.id),
			keyword.name, MaterialParameterSemantic::None,
			value);
	}
	return material;
}

void Engine::ShaderGraphArtifactCache::ApplyToMaterial(
	const ShaderGraphArtifact& artifact,
	MaterialAsset& material) {

	if (MaterialPassBinding* pass =
		FindPass(material, MaterialPassKind::ZPrepass)) {
		if (artifact.depthPipelineID) {
			pass->pipeline = artifact.depthPipelineID;
		}
		pass->shaderOverride = artifact.depthShaderID;
	}
	if (MaterialPassBinding* pass =
		FindPass(material, MaterialPassKind::EditorPicking)) {
		if (artifact.pickingPipelineID) {
			pass->pipeline = artifact.pickingPipelineID;
		}
		pass->shaderOverride = artifact.pickingShaderID;
	}
	if (MaterialPassBinding* pass =
		FindPass(material, MaterialPassKind::Draw)) {
		if (artifact.opaquePipelineID) {
			pass->pipeline = artifact.opaquePipelineID;
		}
		pass->shaderOverride = artifact.opaqueShaderID;
	}
	if (MaterialPassBinding* pass =
		FindPass(material, MaterialPassKind::Transparent)) {
		if (artifact.transparentPipelineID) {
			pass->pipeline = artifact.transparentPipelineID;
		}
		pass->shaderOverride = artifact.transparentShaderID;
	}
	if (MaterialPassBinding* pass =
		FindPass(material, MaterialPassKind::PostProcess)) {
		if (artifact.computePipelineID) {
			pass->pipeline = artifact.computePipelineID;
		}
		pass->shaderOverride = artifact.computeShaderID;
	}
	if (MaterialPassBinding* pass =
		FindPass(material, MaterialPassKind::RayTracing)) {
		if (artifact.rayTracingPipelineID) {
			pass->pipeline = artifact.rayTracingPipelineID;
		}
		pass->shaderOverride = artifact.rayTracingShaderID;
	}
}

Engine::AssetID Engine::ShaderGraphArtifactCache::MakeDerivedID(
	AssetID graphID, uint64_t discriminator) {

	auto hash = [](uint64_t seed, uint64_t value) {

		for (uint32_t byte = 0; byte < 8; ++byte) {
			seed ^= static_cast<uint8_t>(value >> (byte * 8));
			seed *= 1099511628211ull;
		}
		return seed;
	};

	uint64_t high = hash(14695981039346656037ull, graphID.high);
	high = hash(high, graphID.low);
	high = hash(high, discriminator);
	uint64_t low = hash(1099511628211ull, graphID.low);
	low = hash(low, graphID.high);
	low = hash(low, discriminator ^ 0x9e3779b97f4a7c15ull);
	if (high == 0 && low == 0) {
		low = 1;
	}
	return AssetID{ high, low };
}
