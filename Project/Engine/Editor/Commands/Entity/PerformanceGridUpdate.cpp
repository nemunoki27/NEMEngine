#include "SetPerformanceGridCommand.h"
#include "PerformanceGridUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

// c++
#include <vector>

using namespace Engine::PerformanceGridUtility;

bool Engine::SetPerformanceGridCommand::TryUpdateGrid(Engine::EditorCommandContext& context, const Engine::Entity& root) {

	Engine::ECSWorld* world = context.GetWorld();
	const Engine::HierarchyComponent* rootHierarchy =
		world ? world->TryGetComponent<Engine::HierarchyComponent>(root) : nullptr;
	if (!world || !rootHierarchy) {
		return false;
	}

	// 既存のモデルとライトを配置順に収集
	std::vector<Engine::Entity> modelEntities;
	std::vector<Engine::Entity> pointLightEntities;
	Engine::Entity child = rootHierarchy->firstChild;
	while (world->IsAlive(child)) {

		const Engine::HierarchyComponent* childHierarchy = world->TryGetComponent<Engine::HierarchyComponent>(child);
		const Engine::Entity next = childHierarchy ? childHierarchy->nextSibling : Engine::Entity::Null();
		const bool hasRenderer = world->HasComponent<Engine::MeshRendererComponent>(child);
		const bool hasPointLight = world->HasComponent<Engine::PointLightComponent>(child);
		if (hasRenderer == hasPointLight) {
			return false;
		}
		if (hasRenderer) {

			const bool hasAnimation = world->HasComponent<Engine::SkinnedAnimationComponent>(child);
			const bool hasAnimationRuntime = world->HasComponent<Engine::SkinnedAnimationRuntimeComponent>(child);
			if (hasAnimation != playSkinnedAnimation_ || hasAnimationRuntime != playSkinnedAnimation_) {
				return false;
			}
			modelEntities.emplace_back(child);
		} else {
			pointLightEntities.emplace_back(child);
		}
		child = next;
	}

	const size_t modelCount =
		static_cast<size_t>(gridCountXZ_) * static_cast<size_t>(gridCountXZ_) * static_cast<size_t>(gridCountY_);
	const size_t pointLightCount = CalculatePointLightCount(gridCountXZ_, gridCountY_, placePointLights_, pointLightCount_);
	if (modelEntities.size() != modelCount || pointLightEntities.size() != pointLightCount) {
		return false;
	}

	// 同じ配置数ならEntityを再利用
	modelStableUUIDs_.clear();
	modelStableUUIDs_.reserve(modelCount);
	pointLightStableUUIDs_.clear();
	pointLightStableUUIDs_.reserve(pointLightCount);
	for (const Engine::Entity& entity : modelEntities) {
		modelStableUUIDs_.emplace_back(world->GetUUID(entity));
	}
	for (const Engine::Entity& entity : pointLightEntities) {
		pointLightStableUUIDs_.emplace_back(world->GetUUID(entity));
	}

	std::vector<Engine::SubMeshMaterial> subMeshes;
	bool requiresMeshUpdate = false;
	for (const Engine::Entity& entity : modelEntities) {
		if (world->GetComponent<Engine::MeshRendererComponent>(entity).mesh != model_) {
			requiresMeshUpdate = true;
			break;
		}
	}
	if (requiresMeshUpdate) {
		Engine::MeshSubMeshAuthoring::SyncComponentToLayout(layout_, subMeshes, false);
	}

	bool transformChanged = false;
	// モデルの配置順と座標を生成時と更新時で共有
	VisitModelGrid(gridCountXZ_, gridCountY_, gridWidth_, [&](size_t entityIndex, const Engine::Vector3& localPos) {
		const Engine::Entity entity = modelEntities[entityIndex];
		auto& transform = world->GetComponent<Engine::TransformComponent>(entity);
		if (transform.localPos != localPos) {
			transform.localPos = localPos;
			transformChanged = true;
		}

		auto& renderer = world->GetComponent<Engine::MeshRendererComponent>(entity);
		if (renderer.mesh != model_) {
			renderer.mesh = model_;
			renderer.material = {};
			renderer.queue = Engine::RenderPhase::Opaque;
			renderer.visible = true;
			renderer.enableZPrepass = true;
			world->MarkComponentModified<Engine::MeshRendererComponent>(entity);
			Engine::SetMeshSubMeshes(*world, entity, subMeshes);
		}
	});

	if (placePointLights_) {
		const float shadowStrength = pointLightShadows_ ? Engine::PointLightComponent{}.shadowStrength : 0.0f;
		// セル中央から指定数のライトを均等に選ぶ
		VisitPointLightGrid(
			gridCountXZ_, gridCountY_, gridWidth_, pointLightCount, [&](size_t lightIndex, const Engine::Vector3& localPos) {
				const Engine::Entity entity = pointLightEntities[lightIndex];
				auto& transform = world->GetComponent<Engine::TransformComponent>(entity);
				if (transform.localPos != localPos) {
					transform.localPos = localPos;
					transformChanged = true;
				}

				auto& light = world->GetComponent<Engine::PointLightComponent>(entity);
				const Engine::Color4 color = MakePointLightColor(lightIndex);
				if (light.color != color || light.intensity != pointLightIntensity_ || light.radius != pointLightRadius_ ||
					light.decay != pointLightDecay_ || light.shadowStrength != shadowStrength) {

					light.color = color;
					light.intensity = pointLightIntensity_;
					light.radius = pointLightRadius_;
					light.decay = pointLightDecay_;
					light.shadowStrength = shadowStrength;
					world->MarkComponentModified<Engine::PointLightComponent>(entity);
				}
			});
	}

	if (transformChanged) {
		Engine::MarkTransformSubtreeDirty(*world, root);
	}
	return true;
}
