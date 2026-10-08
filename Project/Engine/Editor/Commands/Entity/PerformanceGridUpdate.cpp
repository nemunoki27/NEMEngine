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
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

using namespace Engine;
using namespace Engine::PerformanceGridUtility;

bool Engine::SetPerformanceGridCommand::TryUpdateGrid(Engine::EditorCommandContext& context, const Engine::Entity& root) {

	Engine::ECSWorld* world = context.GetWorld();
	const Engine::HierarchyComponent* rootHierarchy =
		world ? world->TryGetComponent<Engine::HierarchyComponent>(root) : nullptr;
	if (!world || !rootHierarchy) {
		return false;
	}
	const auto lifetime = world->GetLifetime();

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

	// 変更値と復元値を適用前に揃える
	struct ModelUpdate {

		Entity entity;									// 更新対象
		Vector3 previousPosition;						// 更新前の座標
		Vector3 position;								// 更新後の座標
		MeshRendererComponent renderer;					// 更新前の描画設定
		bool meshChanged = false;						// メッシュ差替えの有無
		std::vector<SubMeshMaterial> previousSubMeshes; // 更新前の編集値
		std::vector<SubMeshMaterial> subMeshes;			// 差替え用の編集値
	};
	struct LightUpdate {

		Entity entity;				  // 更新対象
		Vector3 previousPosition;	  // 更新前の座標
		Vector3 position;			  // 更新後の座標
		PointLightComponent previous; // 更新前の照明設定
		PointLightComponent light;	  // 更新後の照明設定
		bool changed = false;		  // 照明設定の変更
	};
	static_assert(std::is_nothrow_move_constructible_v<SubMeshMaterial>);
	std::vector<ModelUpdate> models;
	std::vector<LightUpdate> lights;
	std::vector<UUID> modelIDs;
	std::vector<UUID> lightIDs;
	models.reserve(modelCount);
	lights.reserve(pointLightCount);
	modelIDs.reserve(modelCount);
	lightIDs.reserve(pointLightCount);
	std::vector<SubMeshMaterial> subMeshes;
	bool layoutPrepared = false;
	bool transformChanged = false;
	VisitModelGrid(gridCountXZ_, gridCountY_, gridWidth_, [&](size_t index, const Vector3& position) {
		const Entity entity = modelEntities[index];
		const auto& renderer = world->GetComponent<MeshRendererComponent>(entity);
		ModelUpdate update;
		update.entity = entity;
		update.previousPosition = world->GetComponent<TransformComponent>(entity).localPos;
		update.position = position;
		update.renderer = renderer;
		update.meshChanged = renderer.mesh != model_;
		if (update.meshChanged) {
			auto buffer = world->TryGetBuffer<SubMeshMaterial>(entity);
			if (!buffer.IsValid()) {
				throw std::runtime_error("グリッドのサブメッシュBufferがありません");
			}
			const auto values = buffer.GetSpan();
			update.previousSubMeshes.assign(values.begin(), values.end());
			if (!layoutPrepared) {
				// 共通レイアウトのIDと抽出結果を全モデルで共有
				MeshSubMeshAuthoring::SyncComponentToLayout(layout_, subMeshes, false);
				layoutPrepared = true;
			}
			update.subMeshes = subMeshes;
			// 差替えと復元の両方に必要な容量を確保
			buffer.Reserve(static_cast<uint32_t>((std::max)(values.size(), update.subMeshes.size())));
		}
		transformChanged |= update.previousPosition != position;
		models.emplace_back(std::move(update));
		modelIDs.emplace_back(world->GetUUID(entity));
	});
	const float shadowStrength = pointLightShadows_ ? PointLightComponent{}.shadowStrength : 0.0f;
	VisitPointLightGrid(gridCountXZ_, gridCountY_, gridWidth_, pointLightCount, [&](size_t index, const Vector3& position) {
		const Entity entity = pointLightEntities[index];
		const auto& previous = world->GetComponent<PointLightComponent>(entity);
		LightUpdate update;
		update.entity = entity;
		update.previousPosition = world->GetComponent<TransformComponent>(entity).localPos;
		update.position = position;
		update.previous = previous;
		update.light = previous;
		update.light.color = MakePointLightColor(index);
		update.light.intensity = pointLightIntensity_;
		update.light.radius = pointLightRadius_;
		update.light.decay = pointLightDecay_;
		update.light.shadowStrength = shadowStrength;
		update.changed = previous.color != update.light.color || previous.intensity != update.light.intensity ||
						 previous.radius != update.light.radius || previous.decay != update.light.decay ||
						 previous.shadowStrength != shadowStrength;
		transformChanged |= update.previousPosition != position;
		lights.emplace_back(std::move(update));
		lightIDs.emplace_back(world->GetUUID(entity));
	});

	// 確保済みのBufferへ完成した値を移す
	const auto replaceSubMeshes = [&](const Entity& entity, std::vector<SubMeshMaterial>& values) {
		auto buffer = world->TryGetBuffer<SubMeshMaterial>(entity);
		if (!buffer.IsValid()) {
			throw std::runtime_error("グリッドのサブメッシュBufferがありません");
		}
		buffer.Clear();
		for (SubMeshMaterial& value : values) {
			buffer.EmplaceBack(std::move(value));
		}
	};
	const auto notify = [&]() {
		std::exception_ptr failure;
		const auto report = [&](auto&& action) {
			try {
				action();
			} catch (...) {
				if (!failure) {
					failure = std::current_exception();
				}
			}
			if (!lifetime->IsAlive()) {
				throw std::runtime_error("グリッド更新中にWorldが終了しました");
			}
			if (context.GetWorld() != world) {
				throw std::runtime_error("グリッド更新中に編集Worldが切り替わりました");
			}
		};
		// 一部の購読先が失敗しても残りへ変更を通知
		for (const ModelUpdate& update : models) {
			if (update.meshChanged) {
				report([&]() { world->MarkComponentModified<MeshRendererComponent>(update.entity); });
				report([&]() { world->MarkComponentModified<SubMeshMaterial>(update.entity); });
			}
		}
		for (const LightUpdate& update : lights) {
			if (update.changed) {
				report([&]() { world->MarkComponentModified<PointLightComponent>(update.entity); });
			}
		}
		if (transformChanged) {
			report([&]() { MarkTransformSubtreeDirty(*world, root); });
		}
		if (failure) {
			std::rethrow_exception(failure);
		}
	};
	// 通知中の構造変更で復元先を失わない
	world->WithStableStructure([&]() {
		try {
			// 全Entityを更新してから購読先へ公開
			for (ModelUpdate& update : models) {
				world->GetComponent<TransformComponent>(update.entity).localPos = update.position;
				if (update.meshChanged) {
					auto& renderer = world->GetComponent<MeshRendererComponent>(update.entity);
					renderer.mesh = model_;
					renderer.material = {};
					renderer.queue = RenderPhase::Opaque;
					renderer.visible = true;
					renderer.enableZPrepass = true;
					replaceSubMeshes(update.entity, update.subMeshes);
				}
			}
			for (const LightUpdate& update : lights) {
				world->GetComponent<TransformComponent>(update.entity).localPos = update.position;
				world->GetComponent<PointLightComponent>(update.entity) = update.light;
			}
			notify();
		} catch (...) {
			const std::exception_ptr failure = std::current_exception();
			if (!lifetime->IsAlive()) {
				std::rethrow_exception(failure);
			}
			// 通知失敗時もEntityと編集値を更新前へ戻す
			for (ModelUpdate& update : models) {
				world->GetComponent<TransformComponent>(update.entity).localPos = update.previousPosition;
				if (update.meshChanged) {
					world->GetComponent<MeshRendererComponent>(update.entity) = update.renderer;
					replaceSubMeshes(update.entity, update.previousSubMeshes);
				}
			}
			for (const LightUpdate& update : lights) {
				world->GetComponent<TransformComponent>(update.entity).localPos = update.previousPosition;
				world->GetComponent<PointLightComponent>(update.entity) = update.previous;
			}
			try {
				notify();
			} catch (...) {
				// 復元値の通知後も最初の失敗を呼出し元へ返す
			}
			std::rethrow_exception(failure);
		}
	});
	// 成功した配置だけをRedo用の固定IDへ反映
	modelStableUUIDs_ = std::move(modelIDs);
	pointLightStableUUIDs_ = std::move(lightIDs);
	return true;
}
