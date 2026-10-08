#include "ShaderGraphMaterialBuilder.h"
#include "ShaderGraphParameterDefaults.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

namespace Engine::ShaderGraphMaterialBuilder {

	MaterialAsset CreateMaterial(const ShaderGraphAsset& graph, AssetID graphID) {

		// 対象の描画方式から基本Passを作る
		MaterialAsset material{};
		if (graph.domain == ShaderGraphDomain::RayTracingEffect) {
			material.name = graph.name.empty() ? "NewRayTracingFeatureMaterial" : graph.name;
			material.domain = MaterialDomain::RayTracing;
			material.usage = MaterialUsage::Generic;
			material.passes.emplace_back(MaterialPassBinding{
				.passKind = MaterialPassKind::RayTracing,
				.pipeline = BuiltinAssets::Pipelines::RaytracingReflection,
				.preferredVariant = PipelineVariantKind::Raytracing,
			});
		} else if (graph.domain == ShaderGraphDomain::PostProcess) {
			material.name = graph.name.empty() ? "NewPostProcessMaterial" : graph.name;
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
			material.name = graph.name.empty() ? "NewMaterial" : graph.name;
			material.domain = graph.target == ShaderGraphTarget::Sprite || graph.target == ShaderGraphTarget::Text ||
									  graph.target == ShaderGraphTarget::Primitive2D
								  ? MaterialDomain::UI
								  : MaterialDomain::Surface;
			material.usage =
				graph.target == ShaderGraphTarget::Sprite
					? MaterialUsage::Sprite
					: (graph.target == ShaderGraphTarget::Text
							  ? MaterialUsage::Text
							  : ((graph.target == ShaderGraphTarget::Particle || graph.target == ShaderGraphTarget::Trail)
										? MaterialUsage::Particle
										: MaterialUsage::Generic));

			const auto addPass = [&](MaterialPassKind passKind, AssetID pipeline, PipelineVariantKind variant) {
				material.passes.emplace_back(MaterialPassBinding{
					.passKind = passKind,
					.pipeline = pipeline,
					.preferredVariant = variant,
				});
			};
			switch (graph.target) {
			case ShaderGraphTarget::Primitive3D:
				addPass(MaterialPassKind::Draw, BuiltinAssets::Pipelines::DefaultPrimitive, PipelineVariantKind::GraphicsMesh);
				addPass(MaterialPassKind::Transparent, BuiltinAssets::Pipelines::DefaultPrimitiveTransparent,
					PipelineVariantKind::GraphicsMesh);
				break;
			case ShaderGraphTarget::Sprite:
				addPass(MaterialPassKind::Draw, BuiltinAssets::Pipelines::DefaultSprite, PipelineVariantKind::GraphicsVertex);
				addPass(MaterialPassKind::ScreenSpaceOutlineMask, BuiltinAssets::Pipelines::SpriteOutlineMask,
					PipelineVariantKind::GraphicsVertex);
				addPass(MaterialPassKind::ScreenSpaceOutlineCoverageMask, BuiltinAssets::Pipelines::SpriteOutlineMask,
					PipelineVariantKind::GraphicsVertex);
				break;
			case ShaderGraphTarget::Text:
				addPass(MaterialPassKind::Draw, BuiltinAssets::Pipelines::DefaultText, PipelineVariantKind::GraphicsVertex);
				break;
			case ShaderGraphTarget::Primitive2D:
				addPass(
					MaterialPassKind::Draw, BuiltinAssets::Pipelines::DefaultPrimitive2D, PipelineVariantKind::GraphicsVertex);
				addPass(MaterialPassKind::ScreenSpaceOutlineMask, BuiltinAssets::Pipelines::Primitive2DOutlineMask,
					PipelineVariantKind::GraphicsVertex);
				addPass(MaterialPassKind::ScreenSpaceOutlineCoverageMask, BuiltinAssets::Pipelines::Primitive2DOutlineMask,
					PipelineVariantKind::GraphicsVertex);
				break;
			case ShaderGraphTarget::Particle:
				addPass(MaterialPassKind::Transparent, BuiltinAssets::Pipelines::DefaultParticle,
					PipelineVariantKind::GraphicsVertex);
				break;
			case ShaderGraphTarget::Trail:
				addPass(
					MaterialPassKind::Transparent, BuiltinAssets::Pipelines::ParticleTrail, PipelineVariantKind::GraphicsMesh);
				break;
			case ShaderGraphTarget::Mesh:
				break;
			}
		}

		// Graphの描画設定をMaterialへ引き継ぐ
		material.shaderGraph = graphID;
		if (graph.domain == ShaderGraphDomain::Surface) {
			if (IsShaderGraph3DTarget(graph.target) && !FindPass(material, MaterialPassKind::RayTracing)) {

				material.passes.emplace_back(MaterialPassBinding{
					.passKind = MaterialPassKind::RayTracing,
					.pipeline = BuiltinAssets::Pipelines::RaytracingReflection,
					.preferredVariant = PipelineVariantKind::Raytracing,
				});
			}
			material.renderState.overridesRenderer = true;
			material.renderState.surfaceMode =
				graph.surfaceMode == ShaderGraphSurfaceMode::Transparent
					? MaterialSurfaceMode::Transparent
					: (graph.renderState.alphaClipping ? MaterialSurfaceMode::Masked : MaterialSurfaceMode::Opaque);
			material.renderState.phase =
				graph.surfaceMode == ShaderGraphSurfaceMode::Transparent ? RenderPhase::Transparent : RenderPhase::Opaque;
			material.renderState.blendMode = graph.renderState.blendMode;
			material.renderState.castShadows = graph.renderState.castShadows;
			material.renderState.receiveShadows = graph.renderState.receiveShadows;
		}
		// 公開値の初期設定を基本Materialへ加える
		const auto defaults = ShaderGraphParameterDefaults::Build(graph);
		for (const MaterialParameterRecord& record : defaults.GetRecords()) {
			material.parameters.Set(record.id, record.namedValue.first, record.semantic, record.namedValue.second);
		}
		return material;
	}

	void ApplyToMaterial(const ShaderGraphArtifact& artifact, MaterialAsset& material) {

		// 既存の編集値を保ち、展開で増えた初期値だけを加える
		ShaderGraphParameterDefaults::ApplyMissing(artifact.compileOutput.defaultParameters, material.parameters);

		if (MaterialPassBinding* pass = FindPass(material, MaterialPassKind::ZPrepass)) {
			if (artifact.depthPipelineID) {
				pass->pipeline = artifact.depthPipelineID;
			}
			pass->shaderOverride = artifact.depthShaderID;
		}
		if (MaterialPassBinding* pass = FindPass(material, MaterialPassKind::EditorPicking)) {
			if (artifact.pickingPipelineID) {
				pass->pipeline = artifact.pickingPipelineID;
			}
			pass->shaderOverride = artifact.pickingShaderID;
		}
		for (const auto kind : {MaterialPassKind::ScreenSpaceOutlineMask, MaterialPassKind::ScreenSpaceOutlineCoverageMask}) {

			if (MaterialPassBinding* pass = FindPass(material, kind)) {
				if (artifact.outlinePipelineID) {
					pass->pipeline = artifact.outlinePipelineID;
				}
				pass->shaderOverride = artifact.outlineShaderID;
			}
		}
		// 切り抜きも通常描画と同じグラフで評価する
		for (const auto kind : {MaterialPassKind::Draw, MaterialPassKind::Masked}) {
			if (MaterialPassBinding* pass = FindPass(material, kind)) {
				if (artifact.opaquePipelineID) {
					pass->pipeline = artifact.opaquePipelineID;
				}
				pass->shaderOverride = artifact.opaqueShaderID;
			}
		}
		if (MaterialPassBinding* pass = FindPass(material, MaterialPassKind::Transparent)) {
			if (artifact.transparentPipelineID) {
				pass->pipeline = artifact.transparentPipelineID;
			}
			pass->shaderOverride = artifact.transparentShaderID;
		}
		if (MaterialPassBinding* pass = FindPass(material, MaterialPassKind::PostProcess)) {
			if (artifact.computePipelineID) {
				pass->pipeline = artifact.computePipelineID;
			}
			pass->shaderOverride = artifact.computeShaderID;
		}
		if (MaterialPassBinding* pass = FindPass(material, MaterialPassKind::RayTracing)) {
			if (artifact.rayTracingPipelineID) {
				pass->pipeline = artifact.rayTracingPipelineID;
			}
			pass->shaderOverride = artifact.rayTracingShaderID;
		}
	}
}
