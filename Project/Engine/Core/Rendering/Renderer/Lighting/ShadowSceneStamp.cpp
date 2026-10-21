#include "ShadowSceneStamp.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
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

		return !HasRenderTexture(material.parameters, database) && Engine::MaterialRenderPolicy::IsStandardSurface(material);
	}
}

struct Engine::ShadowSceneStampCache::Implementation {

	struct MeshRecord {

		ECSWorld* world;
		Entity entity;
		std::shared_ptr<const ECSWorldLifetime> lifetime;
		AssetID mesh;
	};
	ShadowSceneStamp key;
	const RenderSceneBatch* batch = nullptr;
	std::shared_ptr<const ECSWorldLifetime> lifetime;
	std::unordered_set<AssetID> materials;
	std::unordered_set<WorldEntityKey, WorldEntityKeyHash> entities;
	std::vector<MeshRecord> meshes;
	bool reusable = false;
	uint64_t rebuildCount = 0;
};

Engine::ShadowSceneStampCache::ShadowSceneStampCache() : implementation_(std::make_unique<Implementation>()) {}
Engine::ShadowSceneStampCache::~ShadowSceneStampCache() = default;

uint64_t Engine::ShadowSceneStampCache::GetRebuildCount() const {

	return implementation_->rebuildCount;
}

Engine::ShadowSceneStamp Engine::ShadowSceneStampCache::Build(GraphicsCore& graphicsCore,
	const SceneExecutionContext& context, const RenderPipelineDeps& deps) {

	ShadowSceneStamp result;
	const auto* camera = context.view ? context.view->FindCamera(RenderCameraDomain::Perspective) : nullptr;
	if (!camera || !deps.renderBatch || !deps.assetLibrary) return result;
	result.renderRevision = deps.renderBatch->GetSourceRevision();
	result.materialRevision = *deps.assetLibrary->GetMaterialRevision();
	Algorithm::HashCombine(result.materialRevision, MaterialRenderPolicy::GetDefaultMaterialHash());
	result.pipelineRevision = deps.pipelineCache ? deps.pipelineCache->GetContentRevision() : 0;
	result.textureRevision = graphicsCore.GetTextureUploadService().GetContentRevision() + RuntimeTextureResolver::GetBindingRevision();
	result.worldRevision = context.view->historyWorldRevision;
	const auto& features = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	result.meshShader = features.useMeshShader;
	result.meshLOD = features.useMeshLOD;
	const auto* meshBackend = deps.backendRegistry ?
		dynamic_cast<const MeshRenderBackend*>(deps.backendRegistry->Find(RenderBackendID::Mesh)) : nullptr;
	result.meshRevision = meshBackend ? meshBackend->GetMeshResourceRevision() : 0;
	auto& cache = *implementation_;
	const auto lifetime = context.world ? context.world->GetLifetime() : nullptr;
	if (cache.batch != deps.renderBatch || cache.lifetime != lifetime || cache.key != result) {
		cache.key = result;
		cache.batch = deps.renderBatch;
		cache.lifetime = lifetime;
		cache.materials.clear();
		cache.entities.clear();
		cache.meshes.clear();
		cache.reusable = true;
		++cache.rebuildCount;

		// 動的なMaterialとスキニングを同じ入力判定へまとめる
		for (const auto& item : deps.renderBatch->GetItems()) {

			if (item.backendID != RenderBackendID::Mesh && item.backendID != RenderBackendID::Primitive) continue;
			const auto materialID = context.assetDatabase && deps.materialResolver ? deps.materialResolver->ResolveORDefault(
				*context.assetDatabase, item.material, item.backendID == RenderBackendID::Mesh ? DefaultMaterialSlot::Mesh : DefaultMaterialSlot::Primitive) : item.material;
			if (cache.materials.insert(materialID).second) {
				const auto* material = deps.assetLibrary->LoadMaterial(materialID);
				if (!material || !UsesStaticMaterial(*material, context.assetDatabase)) cache.reusable = false;
			}
			if (item.backendID == RenderBackendID::Mesh && item.world && cache.entities.insert({ item.world, item.entity }).second) {

				for (const auto& subMesh : GetMeshSubMeshes(*item.world, item.entity)) {
					if (HasRenderTexture(subMesh.materialInstance, context.assetDatabase)) cache.reusable = false;
				}
				const auto* payload = deps.renderBatch->GetPayload<MeshRenderPayload>(item);
				if (payload) cache.meshes.push_back({item.world, item.entity, item.world->GetLifetime(), payload->mesh});
			}
			if (item.backendID == RenderBackendID::Primitive) {
				const auto* payload = deps.renderBatch->GetPayload<PrimitiveRenderPayload>(item);
				if (payload && payload->materialInstance && HasRenderTexture(*payload->materialInstance, context.assetDatabase)) {
					cache.reusable = false;
				}
			}
		}
	}
	// ポーズはMaterialの世代とは独立して進む
	for (const auto& record : cache.meshes) {
		SkinnedVertexSource source;
		if (record.lifetime->IsAlive() && meshBackend &&
			meshBackend->FindSkinnedVertexSource(record.world, record.entity, record.mesh, source)) {
			result.skinningGenerations.push_back(source.poseGeneration);
			result.skinningGenerations.push_back(source.bufferGeneration);
		}
	}
	result.reusable = cache.reusable && !graphicsCore.GetTextureUploadService().HasPendingRequests();
	result.geometryGeneration = context.raytracing.geometryGeneration;
	result.cullingMask = camera->cullingMask;
	result.viewProjection = camera->matrices.viewProjectionMatrix;
	result.nearClip = camera->nearClip;
	result.farClip = camera->farClip;
	return result;
}

Engine::ShadowSceneStamp Engine::BuildShadowSceneStamp(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
	const RenderPipelineDeps& deps) {

	if (deps.shadowStamps) return deps.shadowStamps->Build(graphicsCore, context, deps);
	ShadowSceneStampCache cache;
	return cache.Build(graphicsCore, context, deps);
}
