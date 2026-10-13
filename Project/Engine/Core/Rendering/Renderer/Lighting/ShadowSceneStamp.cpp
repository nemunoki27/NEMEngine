#include "ShadowSceneStamp.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/ECS/Entity/WorldEntityKey.h>

// c++
#include <unordered_set>

namespace {

	bool HasRenderTexture(const Engine::MaterialParameterSet& parameters, const Engine::AssetDatabase* database) {

		// Camera出力はAssetの世代が変わらなくても内容が変わる
		for (const auto& record : parameters.GetRecords()) {
			if (const auto* id = std::get_if<Engine::AssetID>(&record.namedValue.second.value)) {
				const auto* meta = database ? database->Find(*id) : nullptr;
				if (meta && meta->type == Engine::AssetType::RenderTexture) return true;
			}
		}
		return false;
	}

	bool UsesStaticMaterial(const Engine::MaterialAsset& material, const Engine::AssetDatabase* database) {

		if (material.shaderGraph || HasRenderTexture(material.parameters, database)) return false;
		// 任意のShaderは時刻で形状や透明度を変えられる
		for (const auto& pass : material.passes) {
			using namespace Engine::BuiltinAssets::Pipelines;
			if (pass.passKind != Engine::MaterialPassKind::Draw && pass.passKind != Engine::MaterialPassKind::Masked &&
				pass.passKind != Engine::MaterialPassKind::ZPrepass && pass.passKind != Engine::MaterialPassKind::Transparent) continue;
			if (pass.shaderOverride || (pass.pipeline != DefaultMesh && pass.pipeline != DefaultMeshMasked &&
				pass.pipeline != DefaultMeshTransparent && pass.pipeline != DefaultMeshZPrepass &&
				pass.pipeline != DefaultPrimitive && pass.pipeline != DefaultPrimitiveTransparent)) return false;
		}
		return true;
	}
}

Engine::ShadowSceneStamp Engine::BuildShadowSceneStamp(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
	const RenderPipelineDeps& deps) {

	ShadowSceneStamp result;
	const auto* camera = context.view ? context.view->FindCamera(RenderCameraDomain::Perspective) : nullptr;
	if (!camera || !deps.renderBatch || !deps.assetLibrary) return result;
	result.renderRevision = deps.renderBatch->GetSourceRevision();
	result.materialRevision = *deps.assetLibrary->GetMaterialRevision();
	result.pipelineRevision = deps.pipelineCache ? deps.pipelineCache->GetContentRevision() : 0;
	result.textureRevision = graphicsCore.GetTextureUploadService().GetContentRevision() + RuntimeTextureResolver::GetBindingRevision();
	result.geometryGeneration = context.raytracing.geometryGeneration;
	result.worldRevision = context.view->historyWorldRevision;
	result.cullingMask = camera->cullingMask;
	result.viewProjection = camera->matrices.viewProjectionMatrix;
	result.nearClip = camera->nearClip;
	result.farClip = camera->farClip;
	const auto& features = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	result.meshShader = features.useMeshShader;
	result.meshLOD = features.useMeshLOD;
	const auto* meshBackend = deps.backendRegistry ?
		dynamic_cast<const MeshRenderBackend*>(deps.backendRegistry->Find(RenderBackendID::Mesh)) : nullptr;
	result.meshRevision = meshBackend ? meshBackend->GetMeshResourceRevision() : 0;
	result.reusable = !graphicsCore.GetTextureUploadService().HasPendingRequests();
	std::unordered_set<AssetID> materials;
	std::unordered_set<WorldEntityKey, WorldEntityKeyHash> entities;

	// 動的なMaterialとスキニングを同じ入力判定へまとめる
	for (const auto& item : deps.renderBatch->GetItems()) {

		if (item.backendID != RenderBackendID::Mesh && item.backendID != RenderBackendID::Primitive) continue;
		if (item.material && materials.insert(item.material).second) {
			const auto* material = deps.assetLibrary->LoadMaterial(item.material);
			if (!material || !UsesStaticMaterial(*material, context.assetDatabase)) result.reusable = false;
		}
		if (item.backendID == RenderBackendID::Mesh && item.world && entities.insert({ item.world, item.entity }).second) {

			for (const auto& subMesh : GetMeshSubMeshes(*item.world, item.entity)) {
				if (HasRenderTexture(subMesh.materialInstance, context.assetDatabase)) result.reusable = false;
			}
			const auto* payload = deps.renderBatch->GetPayload<MeshRenderPayload>(item);
			SkinnedVertexSource source;
			if (payload && meshBackend && meshBackend->FindSkinnedVertexSource(item.world, item.entity, payload->mesh, source)) {
				result.skinningGenerations.push_back(source.poseGeneration);
				result.skinningGenerations.push_back(source.bufferGeneration);
			}
		}
		if (item.backendID == RenderBackendID::Primitive) {
			const auto* payload = deps.renderBatch->GetPayload<PrimitiveRenderPayload>(item);
			if (payload && payload->materialInstance && HasRenderTexture(*payload->materialInstance, context.assetDatabase)) {
				result.reusable = false;
			}
		}
	}
	return result;
}
