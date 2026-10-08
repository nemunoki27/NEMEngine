#include "SetPerformanceGridCommand.h"
#include "PerformanceGridUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Editor/Core/EditorState.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <span>
#include <utility>
#include <vector>

using namespace Engine::PerformanceGridUtility;

Engine::SetPerformanceGridCommand::SetPerformanceGridCommand(Engine::UUID rootStableUUID, Engine::AssetID model,
	int32_t gridCountXZ, int32_t gridCountY, float gridWidth, bool playSkinnedAnimation, bool placePointLights,
	bool pointLightShadows, int32_t pointLightCount, float pointLightIntensity, float pointLightRadius, float pointLightDecay,
	std::vector<Engine::MeshSubMeshLayoutItem> layout)
	: rootStableUUID_(rootStableUUID), model_(model), gridCountXZ_(gridCountXZ), gridCountY_(gridCountY), gridWidth_(gridWidth),
	  playSkinnedAnimation_(playSkinnedAnimation), placePointLights_(placePointLights), pointLightShadows_(pointLightShadows),
	  pointLightCount_(pointLightCount), pointLightIntensity_(pointLightIntensity), pointLightRadius_(pointLightRadius),
	  pointLightDecay_(pointLightDecay), layout_(std::move(layout)) {
}

Engine::SetPerformanceGridCommand::SetPerformanceGridCommand(Engine::UUID rootStableUUID)
	: rootStableUUID_(rootStableUUID), deleteGrid_(true) {
}

bool Engine::SetPerformanceGridCommand::Execute(Engine::EditorCommandContext& context) {

	if (!context.CanEditScene() || !IsValidRequest()) {
		return false;
	}

	Engine::ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	CaptureSceneOwner(context, *world);

	// 初回だけ置き換え前のグリッドをUndo用に保存する
	if (!previousCaptured_) {

		const Engine::UUID sceneInstanceID = sceneInstanceID_;
		std::vector<Engine::Entity> previousRoots = FindPerformanceGridRoots(*world, sceneInstanceID);
		const Engine::Entity trackedRoot = world->FindByUUID(rootStableUUID_);
		if (world->IsAlive(trackedRoot) &&
			std::find(previousRoots.begin(), previousRoots.end(), trackedRoot) == previousRoots.end()) {

			previousRoots.emplace_back(trackedRoot);
		}
		std::vector<Engine::EditorEntityTreeSnapshot> snapshots;
		snapshots.reserve(previousRoots.size());
		for (const Engine::Entity& previousRoot : previousRoots) {

			Engine::EditorEntityTreeSnapshot snapshot;
			Engine::EditorEntitySnapshotUtility::CaptureSubtree(*world, previousRoot, snapshot);
			snapshots.emplace_back(std::move(snapshot));
		}
		// 全ルートの取得後に取消用データを確定する
		previousSnapshots_ = std::move(snapshots);
		previousCaptured_ = true;
	}
	return CreateGrid(context);
}

void Engine::SetPerformanceGridCommand::Undo(Engine::EditorCommandContext& context) {

	Engine::ECSWorld* world = context.GetWorld();
	if (!world) {
		return;
	}

	const Engine::Entity currentRoot = world->FindByUUID(rootStableUUID_);
	EditorEntityTreeSnapshot currentSnapshot;
	if (world->IsAlive(currentRoot)) {
		// Undo開始時の表示を失敗時の復元用に保持
		EditorEntitySnapshotUtility::CaptureSubtree(*world, currentRoot, currentSnapshot);
	}
	const UUID selectedID = context.editorState ? world->GetUUID(context.editorState->selectedEntity) : UUID{};
	try {
		SceneCreationScope creation(*world);
		if (world->IsAlive(currentRoot)) {
			EditorEntitySnapshotUtility::DestroySubtree(*world, currentRoot);
		}
		Entity selected = Entity::Null();
		for (const EditorEntityTreeSnapshot& snapshot : previousSnapshots_) {
			if (snapshot.IsEmpty()) {
				continue;
			}
			const std::vector<Entity> restored = EditorEntitySnapshotUtility::RestoreSubtree(*world, snapshot);
			EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(context, *world, snapshot, restored);
			if (!world->IsAlive(selected)) {
				selected = world->FindByUUID(snapshot.rootStableUUID);
			}
		}
		context.RebuildHierarchyAll();
		if (context.editorState) {
			context.editorState->SelectEntity(world->IsAlive(selected) ? selected : Entity::Null());
		}
		// 全ルートと表示の復元後にUndoを確定
		creation.Commit();
	} catch (...) {
		const std::exception_ptr failure = std::current_exception();
		try {
			// 部分復元を片付けてUndo開始時の配置へ戻す
			if (!currentSnapshot.IsEmpty()) {
				for (const SerializedEntitySnapshot& saved : currentSnapshot.entities) {
					const Entity entity = world->FindByUUID(saved.stableUUID);
					if (world->IsAlive(entity)) {
						world->DestroyEntity(entity);
					}
				}
				world->FlushPendingDestroyEntities();
				SceneCreationScope recovery(*world);
				const std::vector<Entity> restored = EditorEntitySnapshotUtility::RestoreSubtree(*world, currentSnapshot);
				EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(context, *world, currentSnapshot, restored);
				recovery.Commit();
			}
			context.RebuildHierarchyAll();
			if (context.editorState) {
				context.editorState->SelectEntity(world->FindByUUID(selectedID));
			}
		} catch (...) {
			Logger::Output(LogType::Engine, spdlog::level::err, "負荷確認用グリッドのUndo復元に失敗しました");
			throw;
		}
		std::rethrow_exception(failure);
	}
}

bool Engine::SetPerformanceGridCommand::Redo(Engine::EditorCommandContext& context) {

	return CreateGrid(context);
}

bool Engine::SetPerformanceGridCommand::CanCoalesce(const Engine::IEditorCommand& next) const {

	const auto* nextGrid = dynamic_cast<const SetPerformanceGridCommand*>(&next);
	return nextGrid && nextGrid->rootStableUUID_ == rootStableUUID_;
}

bool Engine::SetPerformanceGridCommand::ExecuteCoalesced(Engine::IEditorCommand& next, Engine::EditorCommandContext& context) {

	auto* nextGrid = dynamic_cast<SetPerformanceGridCommand*>(&next);
	if (!nextGrid || nextGrid->rootStableUUID_ != rootStableUUID_ || !nextGrid->CreateGrid(context)) {
		return false;
	}

	// 取消状態を保って再実行用の設定を更新
	model_ = nextGrid->model_;
	gridCountXZ_ = nextGrid->gridCountXZ_;
	gridCountY_ = nextGrid->gridCountY_;
	gridWidth_ = nextGrid->gridWidth_;
	playSkinnedAnimation_ = nextGrid->playSkinnedAnimation_;
	placePointLights_ = nextGrid->placePointLights_;
	pointLightShadows_ = nextGrid->pointLightShadows_;
	pointLightCount_ = nextGrid->pointLightCount_;
	pointLightIntensity_ = nextGrid->pointLightIntensity_;
	pointLightRadius_ = nextGrid->pointLightRadius_;
	pointLightDecay_ = nextGrid->pointLightDecay_;
	deleteGrid_ = nextGrid->deleteGrid_;
	layout_ = std::move(nextGrid->layout_);
	modelStableUUIDs_ = std::move(nextGrid->modelStableUUIDs_);
	pointLightStableUUIDs_ = std::move(nextGrid->pointLightStableUUIDs_);
	return true;
}

bool Engine::SetPerformanceGridCommand::CreateGrid(Engine::EditorCommandContext& context) {

	// Redoと連続編集も破棄前に入力を検証
	if (!context.CanEditScene() || !IsValidRequest()) {
		return false;
	}

	Engine::ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	CaptureSceneOwner(context, *world);
	const Engine::UUID sceneInstanceID = sceneInstanceID_;
	std::vector<Engine::Entity> previousRoots = FindPerformanceGridRoots(*world, sceneInstanceID);
	const Engine::Entity trackedRoot = world->FindByUUID(rootStableUUID_);
	if (world->IsAlive(trackedRoot) &&
		std::find(previousRoots.begin(), previousRoots.end(), trackedRoot) == previousRoots.end()) {

		previousRoots.emplace_back(trackedRoot);
	}
	if (!deleteGrid_ && world->IsAlive(trackedRoot) && previousRoots.size() == 1 && previousRoots.front() == trackedRoot &&
		TryUpdateGrid(context, trackedRoot)) {
		return true;
	}
	// 旧グリッドを消す前に配置数と固定IDを検証
	const size_t modelCount =
		static_cast<size_t>(gridCountXZ_) * static_cast<size_t>(gridCountXZ_) * static_cast<size_t>(gridCountY_);
	const size_t pointLightCount = CalculatePointLightCount(gridCountXZ_, gridCountY_, placePointLights_, pointLightCount_);
	if (!deleteGrid_) {
		if (modelStableUUIDs_.empty()) {

			modelStableUUIDs_.reserve(modelCount);
			for (size_t index = 0; index < modelCount; ++index) {
				modelStableUUIDs_.emplace_back(Engine::UUID::New());
			}
		}
		if (pointLightStableUUIDs_.empty() && pointLightCount > 0) {

			pointLightStableUUIDs_.reserve(pointLightCount);
			for (size_t index = 0; index < pointLightCount; ++index) {
				pointLightStableUUIDs_.emplace_back(Engine::UUID::New());
			}
		}
		if (modelStableUUIDs_.size() != modelCount || pointLightStableUUIDs_.size() != pointLightCount) {
			return false;
		}
	}

	// 置き換え直前の状態を失敗時の復元用に保存
	std::vector<EditorEntityTreeSnapshot> replacementSnapshots;
	replacementSnapshots.reserve(previousRoots.size());
	for (const Entity& previousRoot : previousRoots) {
		EditorEntityTreeSnapshot snapshot;
		EditorEntitySnapshotUtility::CaptureSubtree(*world, previousRoot, snapshot);
		replacementSnapshots.emplace_back(std::move(snapshot));
	}
	const UUID selectedID = context.editorState ? world->GetUUID(context.editorState->selectedEntity) : UUID{};
	try {
		SceneCreationScope creation(*world);
		for (const Engine::Entity& previousRoot : previousRoots) {
			Engine::EditorEntitySnapshotUtility::DestroySubtree(*world, previousRoot);
		}

		if (deleteGrid_) {

			modelStableUUIDs_.clear();
			pointLightStableUUIDs_.clear();
			context.RebuildHierarchyAll();
			if (context.editorState) {
				context.editorState->SelectEntity(Engine::Entity::Null());
			}
			creation.Commit();
			return true;
		}

		const Engine::Entity root =
			Engine::SceneAuthoring::CreateGameObject(*world, "PerformanceGrid", std::span<const uint32_t>{}, rootStableUUID_);
		SetSceneOwner(*world, root);

		std::vector<Engine::SubMeshMaterial> subMeshes;
		Engine::MeshSubMeshAuthoring::SyncComponentToLayout(layout_, subMeshes, false);

		Engine::ComponentTypeRegistry& registry = Engine::ComponentTypeRegistry::GetInstance();
		std::vector<uint32_t> componentTypes{
			registry.GetID<Engine::MeshRendererComponent>(),
		};
		if (playSkinnedAnimation_) {
			componentTypes.emplace_back(registry.GetID<Engine::SkinnedAnimationComponent>());
			componentTypes.emplace_back(registry.GetID<Engine::SkinnedAnimationRuntimeComponent>());
		}

		Engine::HierarchySystem hierarchySystem;
		// モデルの配置順と座標を生成時と更新時で共有
		VisitModelGrid(gridCountXZ_, gridCountY_, gridWidth_, [&](size_t entityIndex, const Engine::Vector3& localPos) {
			const Engine::Entity entity = Engine::SceneAuthoring::CreateGameObject(
				*world, "PerformanceModel", componentTypes, modelStableUUIDs_[entityIndex]);
			SetSceneOwner(*world, entity);

			auto& transform = world->GetComponent<Engine::TransformComponent>(entity);
			transform.localPos = localPos;
			transform.isDirty = true;

			auto& renderer = world->GetComponent<Engine::MeshRendererComponent>(entity);
			renderer.mesh = model_;
			renderer.material = {};
			renderer.queue = Engine::RenderPhase::Opaque;
			renderer.visible = true;
			renderer.enableZPrepass = true;
			Engine::SetMeshSubMeshes(*world, entity, subMeshes);

			hierarchySystem.SetParent(*world, entity, root);
		});

		const std::array<uint32_t, 1> pointLightTypes{
			registry.GetID<Engine::PointLightComponent>(),
		};
		if (placePointLights_) {
			const float shadowStrength = pointLightShadows_ ? Engine::PointLightComponent{}.shadowStrength : 0.0f;
			// セル中央から指定数のライトを均等に選ぶ
			VisitPointLightGrid(gridCountXZ_, gridCountY_, gridWidth_, pointLightCount,
				[&](size_t lightIndex, const Engine::Vector3& localPos) {
					const Engine::Entity entity = Engine::SceneAuthoring::CreateGameObject(
						*world, "PerformancePointLight", pointLightTypes, pointLightStableUUIDs_[lightIndex]);
					SetSceneOwner(*world, entity);

					auto& transform = world->GetComponent<Engine::TransformComponent>(entity);
					transform.localPos = localPos;
					transform.isDirty = true;

					auto& light = world->GetComponent<Engine::PointLightComponent>(entity);
					light.color = MakePointLightColor(lightIndex);
					light.intensity = pointLightIntensity_;
					light.radius = pointLightRadius_;
					light.decay = pointLightDecay_;
					light.shadowStrength = shadowStrength;

					hierarchySystem.SetParent(*world, entity, root);
				});
		}

		// 次の更新で生成した部分木を一括計算
		Engine::MarkTransformSubtreeDirty(*world, root);
		context.RebuildHierarchyAll();
		if (context.editorState) {
			context.editorState->SelectEntity(root);
		}
		creation.Commit();
		return true;
	} catch (...) {
		const std::exception_ptr failure = std::current_exception();
		try {
			// 部分削除された旧配置を固定UUIDで復元
			for (const EditorEntityTreeSnapshot& snapshot : replacementSnapshots) {
				for (const SerializedEntitySnapshot& saved : snapshot.entities) {
					const Entity entity = world->FindByUUID(saved.stableUUID);
					if (world->IsAlive(entity)) {
						world->DestroyEntity(entity);
					}
				}
				world->FlushPendingDestroyEntities();
				const std::vector<Entity> restored = EditorEntitySnapshotUtility::RestoreSubtree(*world, snapshot);
				EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(context, *world, snapshot, restored);
			}
			context.RebuildHierarchyAll();
			if (context.editorState) {
				context.editorState->SelectEntity(world->FindByUUID(selectedID));
			}
		} catch (...) {
			Logger::Output(LogType::Engine, spdlog::level::err, "負荷確認用グリッドの復元に失敗しました");
			throw;
		}
		std::rethrow_exception(failure);
	}
}

bool Engine::SetPerformanceGridCommand::IsValidRequest() const {

	if (!rootStableUUID_) {
		return false;
	}
	if (deleteGrid_) {
		return true;
	}
	return model_ && !layout_.empty() && IsValidGridCount(gridCountXZ_, gridCountY_, placePointLights_, pointLightCount_) &&
		   std::isfinite(gridWidth_) && gridWidth_ >= 0.001f && gridWidth_ <= 100000.0f &&
		   std::isfinite(pointLightIntensity_) && pointLightIntensity_ >= 0.0f && pointLightIntensity_ <= 128.0f &&
		   std::isfinite(pointLightRadius_) && pointLightRadius_ >= 0.0f && pointLightRadius_ <= 512.0f &&
		   std::isfinite(pointLightDecay_) && pointLightDecay_ >= 0.0f && pointLightDecay_ <= 512.0f;
}

void Engine::SetPerformanceGridCommand::CaptureSceneOwner(const EditorCommandContext& context, ECSWorld& world) {

	if (sceneOwnerCaptured_) {
		return;
	}
	// 既存グリッドはActive Sceneの変更後も元の所属を使う
	const auto* owner = world.TryGetComponent<SceneObjectComponent>(world.FindByUUID(rootStableUUID_));
	sceneInstanceID_ = owner ? owner->sceneInstanceID : context.editorContext->activeSceneInstanceID;
	sceneAsset_ = owner ? owner->sourceAsset : context.editorContext->activeSceneAsset;
	sceneOwnerCaptured_ = true;
}

void Engine::SetPerformanceGridCommand::SetSceneOwner(ECSWorld& world, const Entity& entity) const {

	// Redoでも初回のSceneへ所属させる
	auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
	sceneObject.sceneInstanceID = sceneInstanceID_;
	sceneObject.sourceAsset = sceneAsset_;
}
