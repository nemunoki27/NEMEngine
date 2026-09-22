#include "RenderAssetReloadService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineStateCache.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <Engine/Core/Rendering/Raytracing/RayTracingExecutor.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <functional>
#include <unordered_set>

using namespace Engine;

RenderAssetReloadService::RenderAssetReloadService(RenderAssetLibrary& renderAssetLibrary,
	PipelineStateCache& pipelineStateCache,
	RaytracingPipelineStateCache& raytracingPipelineStateCache,
	PostProcessExecutor& postProcessExecutor,
	RayTracingExecutor& rayTracingExecutor)
	: renderAssetLibrary_(renderAssetLibrary),
	pipelineStateCache_(pipelineStateCache),
	raytracingPipelineStateCache_(raytracingPipelineStateCache),
	postProcessExecutor_(postProcessExecutor),
	rayTracingExecutor_(rayTracingExecutor) {
}

void RenderAssetReloadService::ReloadMaterial(AssetID materialAssetID) {

	AssetID oldPipeline{};
	AssetID oldShader{};
	if (const MaterialAsset* oldMaterial =
		renderAssetLibrary_.LoadMaterial(materialAssetID)) {

		if (const MaterialPassBinding* oldPass =
			FindPass(*oldMaterial, MaterialPassKind::RayTracing)) {

			oldPipeline = oldPass->pipeline;
			oldShader = oldPass->shaderOverride;
		}
	}

	// Materialが参照する旧/新DXR構成だけを無効化し、無関係なState Objectを保持する
	renderAssetLibrary_.InvalidateMaterial(materialAssetID);
	materialRenderStateCache_.erase(materialAssetID);
	if (oldPipeline) {
		raytracingPipelineStateCache_.InvalidateByPipelineAsset(oldPipeline);
	}
	if (oldShader) {
		raytracingPipelineStateCache_.InvalidateByShaderAsset(oldShader);
	}
	if (const MaterialAsset* newMaterial =
		renderAssetLibrary_.LoadMaterial(materialAssetID)) {

		if (const MaterialPassBinding* newPass =
			FindPass(*newMaterial, MaterialPassKind::RayTracing)) {

			if (newPass->pipeline) {
				raytracingPipelineStateCache_.InvalidateByPipelineAsset(
					newPass->pipeline);
			}
			if (newPass->shaderOverride) {
				raytracingPipelineStateCache_.InvalidateByShaderAsset(
					newPass->shaderOverride);
			}
		}
	}
	RenderFeatureProfileService::GetInstance().ClearReflection(
		materialAssetID);
}

void RenderAssetReloadService::ReloadShader(AssetID shaderAssetID) {

	// Raster/Compute/DXRが同じShaderAssetを参照できるため全実行キャッシュを無効化する
	renderAssetLibrary_.InvalidateShader(shaderAssetID);
	pipelineStateCache_.InvalidateByShaderAsset(shaderAssetID);
	raytracingPipelineStateCache_.InvalidateByShaderAsset(shaderAssetID);
	postProcessExecutor_.ClearParameterLayoutCache();
	rayTracingExecutor_.ClearParameterLayoutCache();
	RenderFeatureProfileService::GetInstance().ClearReflectionCache();
}

void RenderAssetReloadService::ReloadPipeline(AssetID pipelineAssetID) {

	renderAssetLibrary_.InvalidatePipeline(pipelineAssetID);
	pipelineStateCache_.InvalidateByPipelineAsset(pipelineAssetID);
	raytracingPipelineStateCache_.InvalidateByPipelineAsset(
		pipelineAssetID);
	postProcessExecutor_.ClearParameterLayoutCache();
	rayTracingExecutor_.ClearParameterLayoutCache();
	RenderFeatureProfileService::GetInstance().ClearReflectionCache();
}

bool RenderAssetReloadService::ReloadMaterialDependencies(
	AssetDatabase& assetDatabase, AssetID materialAssetID) {

	const AssetMeta* materialMeta = assetDatabase.Find(materialAssetID);
	if (!materialMeta || materialMeta->type != AssetType::Material) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レンダー機能] 再読み込み対象のMaterialが見つかりません ID={}",
			ToString(materialAssetID));
		return false;
	}

	std::unordered_set<AssetID> visiting{};
	std::unordered_set<AssetID> completed{};
	size_t shaderCount = 0;
	size_t pipelineCount = 0;
	size_t materialCount = 0;
	const std::function<bool(AssetID)> reloadDependency =
		[&](AssetID assetID) {

		if (completed.contains(assetID)) {
			return true;
		}
		if (!visiting.emplace(assetID).second) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レンダー機能] 再読み込み依存関係が循環しています ID={}",
				ToString(assetID));
			return false;
		}

		assetDatabase.RefreshDependencies(assetID);
		for (AssetID dependency : assetDatabase.FindDependencies(assetID)) {
			if (!reloadDependency(dependency)) {
				return false;
			}
		}

		const AssetMeta* meta = assetDatabase.Find(assetID);
		if (!meta) {
			return false;
		}
		if (meta->type == AssetType::Shader) {
			const std::filesystem::path path =
				Algorithm::PathFromUTF8(meta->assetPath);
			if (Algorithm::ToLower(
				Algorithm::PathToUTF8(path.extension())) == ".json") {

				ReloadShader(assetID);
				++shaderCount;
			}
		} else if (meta->type == AssetType::RenderPipeline) {
			ReloadPipeline(assetID);
			++pipelineCount;
		} else if (meta->type == AssetType::Material) {
			ReloadMaterial(assetID);
			++materialCount;
		}

		visiting.erase(assetID);
		completed.emplace(assetID);
		return true;
	};

	const bool reloaded = reloadDependency(materialAssetID);
	if (reloaded) {
		Logger::Output(LogType::Engine,
			"[レンダー機能] シェーダーを再読み込みしました Material={} Shader={} Pipeline={}",
			materialCount, shaderCount, pipelineCount);
	}
	return reloaded;
}

void RenderAssetReloadService::ReloadAsset(AssetDatabase& assetDatabase, AssetID assetID) {

	const AssetMeta* meta = assetDatabase.Find(assetID);
	if (!meta) {
		return;
	}

	// JSONの参照先が変わった場合に備えて逆引き依存関係も更新する
	assetDatabase.RefreshDependencies(assetID);
	if (meta->type == AssetType::Material) {
		ReloadMaterial(assetID);
		return;
	}
	if (meta->type == AssetType::RenderPipeline) {
		ReloadPipeline(assetID);
		return;
	}
	if (meta->type == AssetType::RenderFeatureProfile) {
		renderAssetLibrary_.InvalidateRenderFeatureProfile(assetID);
		RenderFeatureProfileService::GetInstance().Reload();
		return;
	}
	if (meta->type == AssetType::Font) {
		renderAssetLibrary_.InvalidateFont(assetID);
		return;
	}
	if (meta->type == AssetType::ShaderGraph) {
		std::vector<AssetID> affectedGraphs{ assetID };
		const std::vector<AssetID> referencers =
			assetDatabase.FindReferencersRecursive(assetID);
		for (AssetID referencer : referencers) {
			const AssetMeta* referencerMeta = assetDatabase.Find(referencer);
			if (referencerMeta &&
				referencerMeta->type == AssetType::ShaderGraph) {

				affectedGraphs.emplace_back(referencer);
			}
		}

		// 子Sub Graphから親Graphの順で再生成し、循環参照はDatabase側で除外する
		for (AssetID graphID : affectedGraphs) {
			ShaderGraphAsset graph{};
			ShaderGraphArtifact artifact{};
			const std::filesystem::path graphPath =
				assetDatabase.ResolveFullPath(graphID);
			if (graphPath.empty() ||
				!FromJson(JsonAdapter::Load(graphPath, true), graph) ||
				!ShaderGraphArtifactCache::Compile(
					graph, graphID, artifact, &assetDatabase)) {

				continue;
			}
			const std::array shaderIDs{
				artifact.opaqueShaderID,
				artifact.transparentShaderID,
				artifact.depthShaderID,
				artifact.pickingShaderID,
				artifact.computeShaderID,
				artifact.rayTracingShaderID,
			};
			for (AssetID shaderID : shaderIDs) {
				if (shaderID) {
					pipelineStateCache_.InvalidateByShaderAsset(shaderID);
					raytracingPipelineStateCache_.InvalidateByShaderAsset(shaderID);
				}
			}
			renderAssetLibrary_.RegisterDerivedShader(
				std::move(artifact.opaqueShader));
			renderAssetLibrary_.RegisterDerivedShader(
				std::move(artifact.transparentShader));
			renderAssetLibrary_.RegisterDerivedShader(
				std::move(artifact.depthShader));
			renderAssetLibrary_.RegisterDerivedShader(
				std::move(artifact.pickingShader));
			renderAssetLibrary_.RegisterDerivedShader(
				std::move(artifact.computeShader));
			renderAssetLibrary_.RegisterDerivedShader(
				std::move(artifact.rayTracingShader));
			renderAssetLibrary_.RegisterDerivedPipeline(
				std::move(artifact.opaquePipeline));
			renderAssetLibrary_.RegisterDerivedPipeline(
				std::move(artifact.transparentPipeline));
			renderAssetLibrary_.RegisterDerivedPipeline(
				std::move(artifact.depthPipeline));
			renderAssetLibrary_.RegisterDerivedPipeline(
				std::move(artifact.pickingPipeline));
			renderAssetLibrary_.RegisterDerivedPipeline(
				std::move(artifact.computePipeline));
			renderAssetLibrary_.RegisterDerivedPipeline(
				std::move(artifact.rayTracingPipeline));
		}

		for (AssetID referencer : referencers) {
			const AssetMeta* referencerMeta = assetDatabase.Find(referencer);
			if (referencerMeta && referencerMeta->type == AssetType::Material) {
				ReloadMaterial(referencer);
			}
		}
		return;
	}
	if (meta->type != AssetType::Shader) {
		return;
	}

	const std::filesystem::path path = Algorithm::PathFromUTF8(meta->assetPath);
	if (Algorithm::ToLower(Algorithm::PathToUTF8(path.extension())) == ".json") {
		ReloadShader(assetID);
		return;
	}

	// HLSL変更時は参照するshader.jsonを再ロードして依存PSOを再生成する
	const std::vector<AssetID> referencers = assetDatabase.FindReferencers(assetID);
	for (AssetID referencer : referencers) {
		const AssetMeta* referencerMeta = assetDatabase.Find(referencer);
		if (referencerMeta && referencerMeta->type == AssetType::Shader) {
			ReloadShader(referencer);
		} else if (referencerMeta &&
			referencerMeta->type == AssetType::ShaderGraph) {

			ReloadAsset(assetDatabase, referencer);
		}
	}
}

void RenderAssetReloadService::ApplyMaterialRenderStates(RenderSceneBatch& renderBatch) {

	for (RenderItem& item : renderBatch.GetMutableItems()) {

		if (!item.material) {
			continue;
		}

		auto found = materialRenderStateCache_.find(item.material);
		if (found == materialRenderStateCache_.end()) {
			const MaterialAsset* material =
				renderAssetLibrary_.LoadMaterial(item.material);
			const MaterialRenderState state =
				material ? material->renderState :
				MaterialRenderState{};
			found = materialRenderStateCache_.
				emplace(item.material, state).first;
		}

		// 既定MaterialはRenderer設定を保ち、状態を所有するMaterialだけを適用する
		const MaterialRenderState& state = found->second;
		if (state.overridesRenderer) {
			if (!item.surfaceModeOverridden) {
				item.surfaceMode = state.surfaceMode;
			}
			item.blendMode = state.blendMode;
			item.castShadows = state.castShadows;
			item.receiveShadows = state.receiveShadows;
		}
		item.renderPhase = ResolveMaterialRenderPhase(
			item.surfaceMode, item.renderPhase);
		item.blendMode = ResolveMaterialBlendMode(
			item.surfaceMode, item.blendMode);
	}
}

void RenderAssetReloadService::ClearRenderStates() {

	materialRenderStateCache_.clear();
}
