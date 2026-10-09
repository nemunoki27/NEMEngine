#include "ShaderGraphArtifactCache.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphArtifactBuilder.h"
#include "ShaderGraphMaterialBuilder.h"
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFileJournal.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <algorithm>
#include <regex>
#include <type_traits>
#include <utility>

using namespace Engine::ShaderGraphArtifactBuilder;

//============================================================================
//	ShaderGraphArtifactCache classMethods
//============================================================================
bool Engine::ShaderGraphArtifactCache::Compile(const ShaderGraphAsset& graph, AssetID graphID, ShaderGraphArtifact& outArtifact,
	AssetDatabase* database, std::vector<ShaderGraphDiagnostic>* diagnostics) {

	// 成功するまで呼出し元の成果物を維持
	ShaderGraphArtifact artifact;
	if (diagnostics) {
		diagnostics->clear();
	}
	if (!graphID) {
		return false;
	}

	const std::string graphIDText = ToString(graphID);
	const std::string targetName = graph.domain == ShaderGraphDomain::PostProcess
									   ? "PostProcess"
									   : std::string(EnumAdapter<ShaderGraphTarget>::ToString(graph.target));
	artifact.root = RuntimePaths::GetLibraryPath(Algorithm::PathFromUTF8("ShaderGraph/" + graphIDText + "/" + targetName));
	artifact.surfacePath = artifact.root / "surface.generated.hlsli";
	artifact.opaquePixelPath = artifact.root / "opaque.PS.hlsl";
	artifact.transparentPixelPath = artifact.root / "transparent.PS.hlsl";
	artifact.depthPixelPath = artifact.root / "depth.PS.hlsl";
	artifact.pickingPixelPath = artifact.root / "picking.PS.hlsl";
	artifact.outlinePixelPath = artifact.root / "outline.PS.hlsl";
	artifact.vertexPath = artifact.root / "vertex.VS.hlsl";
	artifact.meshPath = artifact.root / "mesh.MS.hlsl";
	artifact.computePath = artifact.root / "postProcess.CS.hlsl";
	artifact.rayTracingPath = artifact.root / "rayTracing.RT.hlsl";
	std::vector<JsonFileChange> sources;
	const auto queueSource = [&](const std::filesystem::path& path, const std::string& source) {
		JsonFileChange change;
		change.path = path;
		change.bytes = source;
		sources.emplace_back(std::move(change));
	};
	const auto publishArtifact = [&] {
		const JsonFileJournal::Scope scope{
			.recoveryRoot = artifact.root / "Recovery",
			.isWritable = [root = artifact.root](
							  const std::filesystem::path& path) { return StorageFileUtility::IsInside(path, root); },
		};
		std::string error;
		const auto recover = [&](const std::filesystem::path& directory, std::string& diagnostic) {
			return JsonFileJournal::Recover(scope, directory, diagnostic, {});
		};
		// 前回中断した生成ソースを復旧してから保存する
		if (!JsonFileJournal::RecoverPending(scope, error)) {
			Logger::Output(LogType::Engine, spdlog::level::err, "[ShaderGraph] 中断した成果物を復旧できません graph={} 詳細={}",
				ToString(graphID), error);
			return false;
		}
		// 全Passの構成と保存が揃ってから結果を公開
		if (!JsonFileJournal::Commit(scope, sources, "ShaderGraph成果物生成", error, recover)) {
			Logger::Output(LogType::Engine, spdlog::level::err, "[ShaderGraph] 成果物を保存できません graph={} 詳細={}",
				ToString(graphID), error);
			return false;
		}
		static_assert(std::is_nothrow_move_assignable_v<ShaderGraphArtifact>);
		outArtifact = std::move(artifact);
		return true;
	};

	// SubGraphと外部関数の参照を解決する
	const ShaderGraphAssetResolver resolver = [database](AssetID assetID, ShaderGraphAsset& outGraph) {
		if (!database) {
			return false;
		}
		const std::filesystem::path path = database->ResolveFullPath(assetID);
		return !path.empty() && FromJson(JsonAdapter::Load(path, true), outGraph);
	};
	ShaderGraphAsset resolvedGraph = graph;
	if (database) {
		for (ShaderGraphNode& node : resolvedGraph.nodes) {
			if (node.customFunctionSource != ShaderGraphCustomFunctionSource::File || !node.functionFileAsset) {

				continue;
			}
			const std::filesystem::path functionPath = database->ResolveFullPath(node.functionFileAsset);
			node.functionFile = Algorithm::PathToUTF8(functionPath.lexically_normal());
			std::replace(node.functionFile.begin(), node.functionFile.end(), '\\', '/');
		}
	}
	// Graphを解析してShaderソースを作る
	artifact.compileOutput =
		ShaderGraphCompiler::Compile(resolvedGraph, Algorithm::PathToUTF8(artifact.surfacePath.filename()), resolver);
	if (diagnostics) {
		*diagnostics = artifact.compileOutput.diagnostics;
	}
	if (!artifact.compileOutput.Succeeded()) {
		for (const ShaderGraphDiagnostic& diagnostic : artifact.compileOutput.diagnostics) {

			Logger::Output(LogType::Engine, spdlog::level::err, "[ShaderGraph] graph={} node={} stage={} 内容={}",
				ToString(graphID), ToString(diagnostic.node), EnumAdapter<ShaderGraphStage>::ToString(diagnostic.stage),
				diagnostic.message);
		}
		return false;
	}
	// PostProcessの構成を作る
	if (graph.domain == ShaderGraphDomain::PostProcess) {
		queueSource(artifact.computePath, artifact.compileOutput.computeHLSL);
		artifact.computeShaderID = MakeDerivedID(graphID, 0x504f535450524f43ull);
		artifact.computeShader = MakeComputeShader(
			graph.name + "Compute", artifact.computeShaderID, artifact.computePath, artifact.compileOutput.parameters);
		if (!MakeGraphPipeline(graph, artifact.compileOutput, graphID, false, database, artifact.computePipeline,
				artifact.computePipelineID, BuiltinAssets::Pipelines::PostProcessMaskComposite, 0x504f535450495045ull,
				"ComputePipeline")) {
			return false;
		}
		if (!publishArtifact()) {
			return false;
		}
		Logger::Output(LogType::Engine, "[ShaderGraph] コンパイル完了 graph={} target=PostProcess shader={}", ToString(graphID),
			ToString(outArtifact.computeShaderID));
		return true;
	}
	// RayTracingの構成を作る
	if (graph.domain == ShaderGraphDomain::RayTracingEffect) {
		if (artifact.compileOutput.rayTracingHLSL.empty()) {

			return false;
		}
		queueSource(artifact.rayTracingPath, artifact.compileOutput.rayTracingHLSL);
		artifact.rayTracingShaderID = MakeDerivedID(graphID, 0x5241594645415455ull);
		artifact.rayTracingShader = MakeRayTracingShader(graph.name + "RayTracingFeature", artifact.rayTracingShaderID,
			artifact.rayTracingPath, artifact.compileOutput.parameters, true);
		if (!MakeRayTracingPipeline(
				graph, artifact.compileOutput, graphID, database, artifact.rayTracingPipeline, artifact.rayTracingPipelineID)) {

			return false;
		}
		if (!publishArtifact()) {
			return false;
		}
		Logger::Output(LogType::Engine, "[ShaderGraph] コンパイル完了 graph={} target=RayTracingFeature shader={}",
			ToString(graphID), ToString(outArtifact.rayTracingShaderID));
		return true;
	}
	// GIの変形頂点をBLASへ渡すComputeを保存
	if (!artifact.compileOutput.giVertexHLSL.empty()) {

		const auto path = artifact.root / "giVertex.CS.hlsl";
		queueSource(path, artifact.compileOutput.giVertexHLSL);
		artifact.giVertexShader = MakeComputeShader(graph.name + "GIVertex",
			MakeDerivedID(graphID, 0x4749564552544558ull), path, artifact.compileOutput.parameters);
		artifact.giVertexShader.stages[0].profile = "cs_6_6";
	}
	// GIのCallableはGraphごとにExportとSampler領域を分ける
	if (!artifact.compileOutput.giMaterialHLSL.empty()) {

		artifact.giMaterialShaderID = MakeDerivedID(graphID, 0x47494d415445524cull);
		const auto path = artifact.root / "giMaterial.RT.hlsl";
		const auto surface = artifact.root / "giSurface.generated.hlsli";
		std::string surfaceSource = artifact.compileOutput.surfaceHLSL;
		const auto space = 16u + static_cast<uint32_t>(graphID.low & 0x7fffffffu);
		surfaceSource = std::regex_replace(surfaceSource, std::regex("register\\(s([0-9]+)\\)"),
			"register(s$1, space" + std::to_string(space) + ")");
		queueSource(surface, surfaceSource);
		std::string source = artifact.compileOutput.giMaterialHLSL;
		const auto include = source.find(Algorithm::PathToUTF8(artifact.surfacePath.filename()));
		if (include != std::string::npos) source.replace(include, artifact.surfacePath.filename().string().size(),
			surface.filename().string());
		const std::string entry = "GIMaterial_" + graphIDText;
		const auto exportPosition = source.find("void GIMaterial(");
		source.replace(exportPosition + 5, 10, entry);
		queueSource(path, source);
		artifact.giMaterialShader.guid = artifact.giMaterialShaderID;
		artifact.giMaterialShader.name = graph.name + "GI";
		artifact.giMaterialShader.parameters = artifact.compileOutput.parameters;
		artifact.giMaterialShader.stages.push_back({ .stage = ShaderStage::Lib,
			.file = Algorithm::PathToUTF8(path), .entry = entry, .profile = "lib_6_6",
			.ownerShader = artifact.giMaterialShaderID });
	}
	// 描画PassのShaderソースを保存する
	queueSource(artifact.surfacePath, artifact.compileOutput.surfaceHLSL);
	queueSource(artifact.opaquePixelPath, artifact.compileOutput.opaquePixelHLSL);
	queueSource(artifact.transparentPixelPath, artifact.compileOutput.transparentPixelHLSL);
	if (!artifact.compileOutput.outlinePixelHLSL.empty()) {
		queueSource(artifact.outlinePixelPath, artifact.compileOutput.outlinePixelHLSL);
		artifact.outlineShaderID = MakeDerivedID(graphID, 0x4f55544c494e4550ull ^ static_cast<uint64_t>(graph.target));
		artifact.outlineShader = MakePixelShader(graph.name + "Outline", artifact.outlineShaderID, artifact.outlinePixelPath,
			"main", artifact.compileOutput.parameters);
	}
	if (!artifact.compileOutput.rayTracingHLSL.empty()) {
		queueSource(artifact.rayTracingPath, artifact.compileOutput.rayTracingHLSL);
		artifact.rayTracingShaderID = MakeDerivedID(graphID, 0x5241595452414345ull ^ static_cast<uint64_t>(graph.target));
		artifact.rayTracingShader = MakeRayTracingShader(graph.name + "RayTracing", artifact.rayTracingShaderID,
			artifact.rayTracingPath, artifact.compileOutput.parameters, false);
		// GIありの反射を独立したCook成果物へ分ける
		const auto giPath = artifact.root / "giReflection.RT.hlsl";
		queueSource(giPath, "#define NEM_REFLECTION_GI\n" + artifact.compileOutput.rayTracingHLSL);
		artifact.giReflectionShader = MakeRayTracingShader(graph.name + "GIReflection",
			MakeDerivedID(graphID, 0x47495245464c4543ull), giPath, artifact.compileOutput.parameters, false);
		if (!MakeRayTracingPipeline(
				graph, artifact.compileOutput, graphID, database, artifact.rayTracingPipeline, artifact.rayTracingPipelineID)) {

			return false;
		}
	}

	// 不透明と透明のShaderを構成する
	artifact.opaqueShaderID = MakeDerivedID(graphID, 0x4f50415155455f50ull ^ static_cast<uint64_t>(graph.target));
	artifact.transparentShaderID = MakeDerivedID(graphID, 0x5452414e535f5053ull ^ static_cast<uint64_t>(graph.target));
	artifact.opaqueShader = MakePixelShader(
		graph.name + "Opaque", artifact.opaqueShaderID, artifact.opaquePixelPath, "main", artifact.compileOutput.parameters);
	artifact.transparentShader =
		MakePixelShader(graph.name + "Transparent", artifact.transparentShaderID, artifact.transparentPixelPath,
			IsShaderGraph3DTarget(graph.target) ? "mainTransparent" : "main", artifact.compileOutput.parameters);
	// 頂点変形のShader段階を追加する
	const bool hasVertexGraph = !artifact.compileOutput.vertexHLSL.empty();
	const bool hasMeshGraph = !artifact.compileOutput.meshHLSL.empty();
	if (hasVertexGraph) {
		queueSource(artifact.vertexPath, artifact.compileOutput.vertexHLSL);
		if (hasMeshGraph) {
			queueSource(artifact.meshPath, artifact.compileOutput.meshHLSL);
		}
	}
	const auto appendGeneratedGeometryStages = [&](ShaderAsset& shader) {
		shader.stages.emplace_back(ShaderStageEntry{
			.stage = ShaderStage::VS,
			.file = Algorithm::PathToUTF8(artifact.vertexPath),
			.entry = "main",
			.profile = "vs_6_6",
		});
		if (hasMeshGraph) {
			shader.stages.emplace_back(ShaderStageEntry{
				.stage = ShaderStage::MS,
				.file = Algorithm::PathToUTF8(artifact.meshPath),
				.entry = "main",
				.profile = "ms_6_6",
			});
		}
	};
	// Meshの深度と選択Shaderを構成する
	if (graph.target == ShaderGraphTarget::Mesh) {
		queueSource(artifact.depthPixelPath, artifact.compileOutput.depthPixelHLSL);
		queueSource(artifact.pickingPixelPath, artifact.compileOutput.pickingPixelHLSL);
		artifact.depthShaderID = MakeDerivedID(graphID, 0x44455054485f5053ull);
		artifact.pickingShaderID = MakeDerivedID(graphID, 0x5049434b494e4750ull);
		artifact.depthShader = MakePixelShader(
			graph.name + "Depth", artifact.depthShaderID, artifact.depthPixelPath, "main", artifact.compileOutput.parameters);
		artifact.pickingShader = MakePixelShader(graph.name + "Picking", artifact.pickingShaderID, artifact.pickingPixelPath,
			"main", artifact.compileOutput.parameters);

		const std::filesystem::path vertexPath =
			hasVertexGraph ? artifact.vertexPath
						   : RuntimePaths::GetEngineAssetPath("Shaders/Builtin/Mesh/Common/meshGeometry.VS.hlsl");
		const std::filesystem::path meshPath =
			hasVertexGraph ? artifact.meshPath
						   : RuntimePaths::GetEngineAssetPath("Shaders/Builtin/Mesh/Common/meshGeometry.MS.hlsl");
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
		// 切り抜き用のUVを通常Geometryから渡す
		appendGeometryStages(artifact.depthShader);
		if (hasVertexGraph) {
			appendGeometryStages(artifact.opaqueShader);
			appendGeometryStages(artifact.transparentShader);
			artifact.pickingShader.stages.emplace_back(ShaderStageEntry{
				.stage = ShaderStage::VS,
				.file = Algorithm::PathToUTF8(vertexPath),
				.entry = "main",
				.profile = "vs_6_6",
			});
		}
	} else if (hasVertexGraph &&
			   (graph.target == ShaderGraphTarget::Primitive3D || graph.target == ShaderGraphTarget::Primitive2D)) {

		appendGeneratedGeometryStages(artifact.opaqueShader);
		appendGeneratedGeometryStages(artifact.transparentShader);
		if (artifact.outlineShaderID) {
			appendGeneratedGeometryStages(artifact.outlineShader);
		}
	}
	// 全描画PassのPipelineが揃わなければ公開しない
	if (!MakeGraphPipeline(
			graph, artifact.compileOutput, graphID, false, database, artifact.opaquePipeline, artifact.opaquePipelineID) ||
		!MakeGraphPipeline(graph, artifact.compileOutput, graphID, true, database, artifact.transparentPipeline,
			artifact.transparentPipelineID)) {
		return false;
	}
	if (artifact.outlineShaderID) {
		const AssetID basePipeline = graph.target == ShaderGraphTarget::Sprite
										 ? BuiltinAssets::Pipelines::SpriteOutlineMask
										 : BuiltinAssets::Pipelines::Primitive2DOutlineMask;
		if (!MakeGraphPipeline(graph, artifact.compileOutput, graphID, false, database, artifact.outlinePipeline,
				artifact.outlinePipelineID, basePipeline, 0x4f55544c494e4551ull, "OutlinePipeline")) {

			return false;
		}
	}
	// Meshの深度と選択Shaderを構成する
	if (graph.target == ShaderGraphTarget::Mesh) {
		if (!MakeGraphPipeline(graph, artifact.compileOutput, graphID, false, database, artifact.depthPipeline,
				artifact.depthPipelineID, BuiltinAssets::Pipelines::DefaultMeshZPrepass, 0x44455054485f504cull,
				"DepthPipeline") ||
			!MakeGraphPipeline(graph, artifact.compileOutput, graphID, false, database, artifact.pickingPipeline,
				artifact.pickingPipelineID, BuiltinAssets::Pipelines::DefaultMeshEditorPicking, 0x5049434b494e4750ull,
				"PickingPipeline")) {
			return false;
		}
	}
	if (!publishArtifact()) {
		return false;
	}
	Logger::Output(LogType::Engine, "[ShaderGraph] コンパイル完了 graph={} target={} opaqueShader={} transparentShader={}",
		ToString(graphID), EnumAdapter<ShaderGraphTarget>::ToString(graph.target), ToString(outArtifact.opaqueShaderID),
		ToString(outArtifact.transparentShaderID));
	return true;
}

Engine::MaterialAsset Engine::ShaderGraphArtifactCache::CreateMaterial(const ShaderGraphAsset& graph, AssetID graphID) {

	// Materialの構成を専用処理へ渡す
	return ShaderGraphMaterialBuilder::CreateMaterial(graph, graphID);
}

void Engine::ShaderGraphArtifactCache::ApplyToMaterial(const ShaderGraphArtifact& artifact, MaterialAsset& material) {

	// Passへの割当を専用処理へ渡す
	ShaderGraphMaterialBuilder::ApplyToMaterial(artifact, material);
}

Engine::ShaderGraphArtifact Engine::ShaderGraphArtifactCache::DescribeReferences(
	const ShaderGraphAsset& graph, AssetID graphID) {

	ShaderGraphArtifact artifact;
	const auto derived = [&](uint64_t discriminator) { return MakeDerivedID(graphID, discriminator); };
	const auto target = static_cast<uint64_t>(graph.target);
	// PostProcessの構成を作る
	if (graph.domain == ShaderGraphDomain::PostProcess) {
		artifact.computePipelineID = derived(0x504f535450495045ull);
		artifact.computeShaderID = derived(0x504f535450524f43ull);
		return artifact;
	}
	// RayTracingの構成を作る
	if (graph.domain == ShaderGraphDomain::RayTracingEffect) {
		artifact.rayTracingPipelineID = derived(0x5241595452414350ull);
		artifact.rayTracingShaderID = derived(0x5241594645415455ull);
		return artifact;
	}
	artifact.opaquePipelineID = derived(0x4f50415155455f4cull ^ target);
	artifact.opaqueShaderID = derived(0x4f50415155455f50ull ^ target);
	artifact.transparentPipelineID = derived(0x5452414e535f504cull ^ target);
	artifact.transparentShaderID = derived(0x5452414e535f5053ull ^ target);
	if (graph.target == ShaderGraphTarget::Sprite || graph.target == ShaderGraphTarget::Primitive2D) {

		artifact.outlinePipelineID = derived(0x4f55544c494e4551ull);
		artifact.outlineShaderID = derived(0x4f55544c494e4550ull ^ target);
	}
	// Meshの深度と選択Shaderを構成する
	if (graph.target == ShaderGraphTarget::Mesh) {
		artifact.depthPipelineID = derived(0x44455054485f504cull);
		artifact.depthShaderID = derived(0x44455054485f5053ull);
		artifact.pickingPipelineID = derived(0x5049434b494e4750ull);
		artifact.pickingShaderID = artifact.pickingPipelineID;
	}
	if (IsShaderGraph3DTarget(graph.target)) {
		artifact.rayTracingPipelineID = derived(0x5241595452414350ull);
		artifact.rayTracingShaderID = derived(0x5241595452414345ull ^ target);
		artifact.giMaterialShaderID = derived(0x47494d415445524cull);
	}
	return artifact;
}

Engine::AssetID Engine::ShaderGraphArtifactCache::MakeDerivedID(AssetID graphID, uint64_t discriminator) {

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
	return AssetID{high, low};
}
