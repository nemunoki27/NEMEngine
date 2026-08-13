#include "PerformanceCheckTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
// imgui
#include <imgui.h>

//============================================================================
//	PerformanceCheckTool classMethods
//============================================================================
namespace {

	// XZセル中央へ配置するポイントライト数を返す
	size_t CalculatePointLightCount(
		int32_t gridCountXZ, int32_t gridCountY,
		bool enabled, int32_t requestedCount) {

		if (!enabled || gridCountXZ <= 1 ||
			gridCountY <= 0 || requestedCount <= 0) {
			return 0;
		}
		const size_t cellCount =
			static_cast<size_t>(gridCountXZ - 1);
		return (std::min)(
			cellCount * cellCount * static_cast<size_t>(gridCountY),
			static_cast<size_t>(requestedCount));
	}

	// 現在のシーンに生成済みのパフォーマンスグリッドを収集する
	std::vector<Engine::Entity> FindPerformanceGridRoots(
		Engine::ECSWorld& world, Engine::UUID sceneInstanceID) {

		std::vector<Engine::Entity> roots;
		world.ForEach<Engine::NameComponent,
			Engine::SceneObjectComponent,
			Engine::HierarchyComponent>(
				[&](const Engine::Entity& entity,
					Engine::NameComponent& name,
					Engine::SceneObjectComponent& sceneObject,
					Engine::HierarchyComponent& hierarchy) {

					if (name.name != "PerformanceGrid" ||
						world.IsAlive(hierarchy.parent)) {
						return;
					}
					if (sceneInstanceID &&
						sceneObject.sceneInstanceID != sceneInstanceID) {
						return;
					}
					roots.emplace_back(entity);
				});
		return roots;
	}

	// 隣接ライトの色相が偏らないゲーミングカラーを返す
	Engine::Color4 MakeGamingColor(size_t index) {

		constexpr float goldenRatio = 0.61803398875f;
		const float hue = std::fmod(
			static_cast<float>(index) * goldenRatio, 1.0f);
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, r, g, b);
		return Engine::Color4(r, g, b, 1.0f);
	}

	//============================================================================
	//	SetPerformanceGridCommand class
	//	パフォーマンス確認用グリッドを一括生成するコマンド
	//============================================================================
	class SetPerformanceGridCommand final :
		public Engine::IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SetPerformanceGridCommand(Engine::UUID rootStableUUID,
			Engine::AssetID model, int32_t gridCountXZ, int32_t gridCountY,
			float gridWidth, bool playSkinnedAnimation,
			bool placePointLights, bool pointLightShadows,
			int32_t pointLightCount,
			float pointLightIntensity, float pointLightRadius,
			float pointLightDecay,
			std::vector<Engine::MeshSubMeshLayoutItem> layout) :
			rootStableUUID_(rootStableUUID),
			model_(model),
			gridCountXZ_(gridCountXZ),
			gridCountY_(gridCountY),
			gridWidth_(gridWidth),
			playSkinnedAnimation_(playSkinnedAnimation),
			placePointLights_(placePointLights),
			pointLightShadows_(pointLightShadows),
			pointLightCount_(pointLightCount),
			pointLightIntensity_(pointLightIntensity),
			pointLightRadius_(pointLightRadius),
			pointLightDecay_(pointLightDecay),
			layout_(std::move(layout)) {
		}
		explicit SetPerformanceGridCommand(
			Engine::UUID rootStableUUID) :
			rootStableUUID_(rootStableUUID),
			deleteGrid_(true) {
		}
		~SetPerformanceGridCommand() override = default;

		// グリッドを生成する
		bool Execute(Engine::EditorCommandContext& context) override;
		// 生成前のグリッドへ戻す
		void Undo(Engine::EditorCommandContext& context) override;
		// 同じUUIDでグリッドを再生成する
		bool Redo(Engine::EditorCommandContext& context) override;
		// 同じグリッドへの連続再配置を1件のUndoへまとめる
		bool CanCoalesce(const Engine::IEditorCommand& next) const override;
		bool ExecuteCoalesced(Engine::IEditorCommand& next,
			Engine::EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "SetPerformanceGrid"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Engine::UUID rootStableUUID_{};
		Engine::AssetID model_{};
		int32_t gridCountXZ_ = 1;
		int32_t gridCountY_ = 1;
		float gridWidth_ = 1.0f;

		bool playSkinnedAnimation_ = false;
		bool placePointLights_ = false;
		bool pointLightShadows_ = false;
		int32_t pointLightCount_ = 200;
		float pointLightIntensity_ = 1.0f;
		float pointLightRadius_ = 8.0f;
		float pointLightDecay_ = 1.0f;
		bool deleteGrid_ = false;

		std::vector<Engine::MeshSubMeshLayoutItem> layout_;
		std::vector<Engine::UUID> modelStableUUIDs_;
		std::vector<Engine::UUID> pointLightStableUUIDs_;
		std::vector<Engine::EditorEntityTreeSnapshot> previousSnapshots_;
		bool previousCaptured_ = false;

		//--------- functions ----------------------------------------------------

		// グリッドを現在の編集ワールドへ生成する
		bool CreateGrid(Engine::EditorCommandContext& context);
		// 同数の生成済みエンティティを破棄せず設定だけ更新する
		bool TryUpdateGrid(Engine::EditorCommandContext& context,
			const Engine::Entity& root);
		// エンティティへ現在のシーン所属を設定する
		void SetSceneOwner(const Engine::EditorCommandContext& context,
			Engine::ECSWorld& world, const Engine::Entity& entity) const;
	};
}

bool SetPerformanceGridCommand::Execute(Engine::EditorCommandContext& context) {

	if (!context.CanEditScene() || !rootStableUUID_ ||
		(!deleteGrid_ && (!model_ || gridCountXZ_ <= 0 ||
			gridCountY_ <= 0 || layout_.empty()))) {
		return false;
	}

	Engine::ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回だけ置き換え前のグリッドをUndo用に保存する
	if (!previousCaptured_) {

		const Engine::UUID sceneInstanceID = context.editorContext ?
			context.editorContext->activeSceneInstanceID : Engine::UUID{};
		std::vector<Engine::Entity> previousRoots =
			FindPerformanceGridRoots(*world, sceneInstanceID);
		const Engine::Entity trackedRoot =
			world->FindByUUID(rootStableUUID_);
		if (world->IsAlive(trackedRoot) &&
			std::find(previousRoots.begin(), previousRoots.end(),
				trackedRoot) == previousRoots.end()) {

			previousRoots.emplace_back(trackedRoot);
		}
		previousSnapshots_.reserve(previousRoots.size());
		for (const Engine::Entity& previousRoot : previousRoots) {

			Engine::EditorEntityTreeSnapshot snapshot;
			Engine::EditorEntitySnapshotUtility::CaptureSubtree(
				*world, previousRoot, snapshot);
			previousSnapshots_.emplace_back(std::move(snapshot));
		}
		previousCaptured_ = true;
	}
	return CreateGrid(context);
}

void SetPerformanceGridCommand::Undo(Engine::EditorCommandContext& context) {

	Engine::ECSWorld* world = context.GetWorld();
	if (!world) {
		return;
	}

	const Engine::Entity currentRoot = world->FindByUUID(rootStableUUID_);
	if (world->IsAlive(currentRoot)) {
		Engine::EditorEntitySnapshotUtility::DestroySubtree(*world, currentRoot);
	}

	Engine::Entity selected = Engine::Entity::Null();
	for (const Engine::EditorEntityTreeSnapshot& snapshot :
		previousSnapshots_) {

		if (snapshot.IsEmpty()) {
			continue;
		}
		const std::vector<Engine::Entity> restored =
			Engine::EditorEntitySnapshotUtility::RestoreSubtree(
				*world, snapshot);
		Engine::EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(
			context, *world, snapshot, restored);
		if (!world->IsAlive(selected)) {
			selected = world->FindByUUID(snapshot.rootStableUUID);
		}
	}

	context.RebuildHierarchyAll();
	if (context.editorState) {
		context.editorState->SelectEntity(
			world->IsAlive(selected) ? selected : Engine::Entity::Null());
	}
}

bool SetPerformanceGridCommand::Redo(Engine::EditorCommandContext& context) {

	return CreateGrid(context);
}

bool SetPerformanceGridCommand::CanCoalesce(
	const Engine::IEditorCommand& next) const {

	const auto* nextGrid =
		dynamic_cast<const SetPerformanceGridCommand*>(&next);
	return nextGrid &&
		nextGrid->rootStableUUID_ == rootStableUUID_;
}

bool SetPerformanceGridCommand::ExecuteCoalesced(
	Engine::IEditorCommand& next,
	Engine::EditorCommandContext& context) {

	auto* nextGrid =
		dynamic_cast<SetPerformanceGridCommand*>(&next);
	if (!nextGrid || !nextGrid->CreateGrid(context)) {
		return false;
	}

	// 最初のUndoスナップショットは残し、Redoに必要な最新設定だけを引き継ぐ
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
	pointLightStableUUIDs_ =
		std::move(nextGrid->pointLightStableUUIDs_);
	return true;
}

bool SetPerformanceGridCommand::TryUpdateGrid(
	Engine::EditorCommandContext& context,
	const Engine::Entity& root) {

	Engine::ECSWorld* world = context.GetWorld();
	const Engine::HierarchyComponent* rootHierarchy = world ?
		world->TryGetComponent<Engine::HierarchyComponent>(root) : nullptr;
	if (!world || !rootHierarchy) {
		return false;
	}

	std::vector<Engine::Entity> modelEntities;
	std::vector<Engine::Entity> pointLightEntities;
	Engine::Entity child = rootHierarchy->firstChild;
	while (world->IsAlive(child)) {

		const Engine::HierarchyComponent* childHierarchy =
			world->TryGetComponent<Engine::HierarchyComponent>(child);
		const Engine::Entity next = childHierarchy ?
			childHierarchy->nextSibling : Engine::Entity::Null();
		const bool hasRenderer =
			world->HasComponent<Engine::MeshRendererComponent>(child);
		const bool hasPointLight =
			world->HasComponent<Engine::PointLightComponent>(child);
		if (hasRenderer == hasPointLight) {
			return false;
		}
		if (hasRenderer) {

			const bool hasAnimation =
				world->HasComponent<Engine::SkinnedAnimationComponent>(child);
			const bool hasAnimationRuntime =
				world->HasComponent<
					Engine::SkinnedAnimationRuntimeComponent>(child);
			if (hasAnimation != playSkinnedAnimation_ ||
				hasAnimationRuntime != playSkinnedAnimation_) {
				return false;
			}
			modelEntities.emplace_back(child);
		} else {
			pointLightEntities.emplace_back(child);
		}
		child = next;
	}

	const size_t modelCount =
		static_cast<size_t>(gridCountXZ_) *
		static_cast<size_t>(gridCountXZ_) *
		static_cast<size_t>(gridCountY_);
	const size_t pointLightCount = CalculatePointLightCount(
		gridCountXZ_, gridCountY_, placePointLights_,
		pointLightCount_);
	if (modelEntities.size() != modelCount ||
		pointLightEntities.size() != pointLightCount) {
		return false;
	}

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
		if (world->GetComponent<
			Engine::MeshRendererComponent>(entity).mesh != model_) {
			requiresMeshUpdate = true;
			break;
		}
	}
	if (requiresMeshUpdate) {
		Engine::MeshSubMeshAuthoring::SyncComponentToLayout(
			layout_, subMeshes, false);
	}

	const float startX =
		-static_cast<float>(gridCountXZ_ - 1) * gridWidth_ * 0.5f;
	const float startZ =
		-static_cast<float>(gridCountXZ_ - 1) * gridWidth_ * 0.5f;
	bool transformChanged = false;
	size_t entityIndex = 0;
	for (int32_t y = 0; y < gridCountY_; ++y) {
		for (int32_t z = 0; z < gridCountXZ_; ++z) {
			for (int32_t x = 0; x < gridCountXZ_; ++x) {

				const Engine::Entity entity =
					modelEntities[entityIndex++];
				auto& transform =
					world->GetComponent<Engine::TransformComponent>(entity);
				const Engine::Vector3 localPos(
					startX + static_cast<float>(x) * gridWidth_,
					static_cast<float>(y) * gridWidth_,
					startZ + static_cast<float>(z) * gridWidth_);
				if (transform.localPos != localPos) {
					transform.localPos = localPos;
					transformChanged = true;
				}

				auto& renderer =
					world->GetComponent<
						Engine::MeshRendererComponent>(entity);
				if (renderer.mesh != model_) {
					renderer.mesh = model_;
					renderer.material = {};
					renderer.queue = Engine::RenderPhase::Opaque;
					renderer.visible = true;
					renderer.enableZPrepass = true;
					world->MarkComponentModified<
						Engine::MeshRendererComponent>(entity);
					Engine::SetMeshSubMeshes(
						*world, entity, subMeshes);
				}
			}
		}
	}

	size_t lightIndex = 0;
	if (placePointLights_) {
		const float shadowStrength = pointLightShadows_ ?
			Engine::PointLightComponent{}.shadowStrength : 0.0f;
		const size_t totalCellCount =
			static_cast<size_t>(gridCountXZ_ - 1) *
			static_cast<size_t>(gridCountXZ_ - 1) *
			static_cast<size_t>(gridCountY_);
		size_t cellIndex = 0;
		for (int32_t y = 0; y < gridCountY_; ++y) {
			for (int32_t z = 0; z + 1 < gridCountXZ_; ++z) {
				for (int32_t x = 0; x + 1 < gridCountXZ_; ++x) {

					const size_t previousSample =
						cellIndex * pointLightCount / totalCellCount;
					const size_t nextSample =
						(cellIndex + 1) * pointLightCount / totalCellCount;
					++cellIndex;
					if (nextSample == previousSample) {
						continue;
					}

					const Engine::Entity entity =
						pointLightEntities[lightIndex];
					auto& transform =
						world->GetComponent<
							Engine::TransformComponent>(entity);
					const Engine::Vector3 localPos(
						startX +
							(static_cast<float>(x) + 0.5f) * gridWidth_,
						static_cast<float>(y) * gridWidth_,
						startZ +
							(static_cast<float>(z) + 0.5f) * gridWidth_);
					if (transform.localPos != localPos) {
						transform.localPos = localPos;
						transformChanged = true;
					}

					auto& light =
						world->GetComponent<
							Engine::PointLightComponent>(entity);
					const Engine::Color4 color =
						MakeGamingColor(lightIndex);
					if (light.color != color ||
						light.intensity != pointLightIntensity_ ||
						light.radius != pointLightRadius_ ||
						light.decay != pointLightDecay_ ||
						light.shadowStrength != shadowStrength) {

						light.color = color;
						light.intensity = pointLightIntensity_;
						light.radius = pointLightRadius_;
						light.decay = pointLightDecay_;
						light.shadowStrength = shadowStrength;
						world->MarkComponentModified<
							Engine::PointLightComponent>(entity);
					}
					++lightIndex;
				}
			}
		}
	}

	if (transformChanged) {
		Engine::MarkTransformSubtreeDirty(*world, root);
	}
	return true;
}

bool SetPerformanceGridCommand::CreateGrid(
	Engine::EditorCommandContext& context) {

	if (!context.CanEditScene()) {
		return false;
	}

	Engine::ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	const Engine::UUID sceneInstanceID = context.editorContext ?
		context.editorContext->activeSceneInstanceID : Engine::UUID{};
	std::vector<Engine::Entity> previousRoots =
		FindPerformanceGridRoots(*world, sceneInstanceID);
	const Engine::Entity trackedRoot =
		world->FindByUUID(rootStableUUID_);
	if (world->IsAlive(trackedRoot) &&
		std::find(previousRoots.begin(), previousRoots.end(),
			trackedRoot) == previousRoots.end()) {

		previousRoots.emplace_back(trackedRoot);
	}
	if (!deleteGrid_ && world->IsAlive(trackedRoot) &&
		previousRoots.size() == 1 &&
		previousRoots.front() == trackedRoot &&
		TryUpdateGrid(context, trackedRoot)) {
		return true;
	}
	for (const Engine::Entity& previousRoot : previousRoots) {
		Engine::EditorEntitySnapshotUtility::DestroySubtree(
			*world, previousRoot);
	}

	if (deleteGrid_) {

		modelStableUUIDs_.clear();
		pointLightStableUUIDs_.clear();
		context.RebuildHierarchyAll();
		if (context.editorState) {
			context.editorState->SelectEntity(Engine::Entity::Null());
		}
		return true;
	}

	const size_t modelCount =
		static_cast<size_t>(gridCountXZ_) *
		static_cast<size_t>(gridCountXZ_) *
		static_cast<size_t>(gridCountY_);
	const size_t pointLightCount = CalculatePointLightCount(
		gridCountXZ_, gridCountY_, placePointLights_,
		pointLightCount_);
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
	if (modelStableUUIDs_.size() != modelCount ||
		pointLightStableUUIDs_.size() != pointLightCount) {
		return false;
	}

	const Engine::Entity root = Engine::SceneAuthoring::CreateGameObject(
		*world, "PerformanceGrid", std::span<const uint32_t>{}, rootStableUUID_);
	SetSceneOwner(context, *world, root);

	std::vector<Engine::SubMeshMaterial> subMeshes;
	Engine::MeshSubMeshAuthoring::SyncComponentToLayout(
		layout_, subMeshes, false);

	Engine::ComponentTypeRegistry& registry =
		Engine::ComponentTypeRegistry::GetInstance();
	std::vector<uint32_t> componentTypes{
		registry.GetID<Engine::MeshRendererComponent>(),
	};
	if (playSkinnedAnimation_) {
		componentTypes.emplace_back(
			registry.GetID<Engine::SkinnedAnimationComponent>());
		componentTypes.emplace_back(
			registry.GetID<Engine::SkinnedAnimationRuntimeComponent>());
	}

	const float startX =
		-static_cast<float>(gridCountXZ_ - 1) * gridWidth_ * 0.5f;
	const float startZ =
		-static_cast<float>(gridCountXZ_ - 1) * gridWidth_ * 0.5f;

	Engine::HierarchySystem hierarchySystem;
	size_t entityIndex = 0;
	for (int32_t y = 0; y < gridCountY_; ++y) {
		for (int32_t z = 0; z < gridCountXZ_; ++z) {
			for (int32_t x = 0; x < gridCountXZ_; ++x) {

				const Engine::Entity entity =
					Engine::SceneAuthoring::CreateGameObject(
						*world, "PerformanceModel", componentTypes,
						modelStableUUIDs_[entityIndex++]);
				SetSceneOwner(context, *world, entity);

				auto& transform =
					world->GetComponent<Engine::TransformComponent>(entity);
				transform.localPos = Engine::Vector3(
					startX + static_cast<float>(x) * gridWidth_,
					static_cast<float>(y) * gridWidth_,
					startZ + static_cast<float>(z) * gridWidth_);
				transform.isDirty = true;

				auto& renderer =
					world->GetComponent<Engine::MeshRendererComponent>(entity);
				renderer.mesh = model_;
				renderer.material = {};
				renderer.queue = Engine::RenderPhase::Opaque;
				renderer.visible = true;
				renderer.enableZPrepass = true;
				Engine::SetMeshSubMeshes(*world, entity, subMeshes);

				hierarchySystem.SetParent(*world, entity, root);
			}
		}
	}

	const std::array<uint32_t, 1> pointLightTypes{
		registry.GetID<Engine::PointLightComponent>(),
	};
	size_t lightIndex = 0;
	if (placePointLights_) {
		const float shadowStrength = pointLightShadows_ ?
			Engine::PointLightComponent{}.shadowStrength : 0.0f;
		const size_t totalCellCount =
			static_cast<size_t>(gridCountXZ_ - 1) *
			static_cast<size_t>(gridCountXZ_ - 1) *
			static_cast<size_t>(gridCountY_);
		size_t cellIndex = 0;
		for (int32_t y = 0; y < gridCountY_; ++y) {
			for (int32_t z = 0; z + 1 < gridCountXZ_; ++z) {
				for (int32_t x = 0; x + 1 < gridCountXZ_; ++x) {

					// 指定数のライトをグリッド全体から均等に選ぶ
					const size_t previousSample =
						cellIndex * pointLightCount / totalCellCount;
					const size_t nextSample =
						(cellIndex + 1) * pointLightCount / totalCellCount;
					++cellIndex;
					if (nextSample == previousSample) {
						continue;
					}

					const Engine::Entity entity =
						Engine::SceneAuthoring::CreateGameObject(
							*world, "PerformancePointLight", pointLightTypes,
							pointLightStableUUIDs_[lightIndex]);
					SetSceneOwner(context, *world, entity);

					auto& transform =
						world->GetComponent<Engine::TransformComponent>(entity);
					transform.localPos = Engine::Vector3(
						startX + (static_cast<float>(x) + 0.5f) * gridWidth_,
						static_cast<float>(y) * gridWidth_,
						startZ + (static_cast<float>(z) + 0.5f) * gridWidth_);
					transform.isDirty = true;

					auto& light =
						world->GetComponent<Engine::PointLightComponent>(entity);
					light.color = MakeGamingColor(lightIndex);
					light.intensity = pointLightIntensity_;
					light.radius = pointLightRadius_;
					light.decay = pointLightDecay_;
					light.shadowStrength = shadowStrength;

					hierarchySystem.SetParent(*world, entity, root);
					++lightIndex;
				}
			}
		}
	}

	// 大量生成中の個別通知順に依存せず、次のTransformSystem更新で部分木を一括計算する
	Engine::MarkTransformSubtreeDirty(*world, root);
	context.RebuildHierarchyAll();
	if (context.editorState) {
		context.editorState->SelectEntity(root);
	}
	return true;
}

void SetPerformanceGridCommand::SetSceneOwner(
	const Engine::EditorCommandContext& context,
	Engine::ECSWorld& world, const Engine::Entity& entity) const {

	if (!context.editorContext) {
		return;
	}

	auto& sceneObject =
		world.GetComponent<Engine::SceneObjectComponent>(entity);
	sceneObject.sceneInstanceID =
		context.editorContext->activeSceneInstanceID;
	sceneObject.sourceAsset =
		context.editorContext->activeSceneAsset;
}

Engine::PerformanceCheckTool::PerformanceCheckTool() {

	LoadSettings();
}

Engine::PerformanceCheckTool::~PerformanceCheckTool() {

	SaveSettings();
}

void Engine::PerformanceCheckTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::PerformanceCheckTool::DrawEditorTool(
	const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow(context);
	}
}

void Engine::PerformanceCheckTool::DrawWindow(
	const EditorToolContext& context) {

	ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f),
		ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("パフォーマンスチェック", &openWindow_)) {
		ImGui::End();
		return;
	}

	ECSWorld* world = context.GetWorld();
	AssetDatabase* assetDatabase = context.toolContext.assetDatabase;
	IEditorPanelHost* host =
		context.panelContext ? context.panelContext->host : nullptr;
	if (!context.CanEditScene() || !world || !assetDatabase || !host) {
		ImGui::TextDisabled("編集可能なシーンがありません");
		ImGui::End();
		return;
	}

	GridSceneState& sceneState =
		sceneStates_[context.toolContext.activeSceneInstanceID];
	Entity root = sceneState.rootStableUUID ?
		world->FindByUUID(sceneState.rootStableUUID) : Entity::Null();
	if (!world->IsAlive(root)) {

		const std::vector<Entity> existingRoots =
			FindPerformanceGridRoots(
				*world, context.toolContext.activeSceneInstanceID);
		if (!existingRoots.empty()) {

			root = existingRoots.front();
			sceneState.rootStableUUID = world->GetUUID(root);
		}
	}
	if (root != sceneState.observedRoot) {
		RefreshStatistics(context, sceneState, root);
	}

	{
		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"PerformanceCheckSettings");
		MyGUI::DragInt("XZグリッド数", gridCountXZ_, {
			.minValue = 1,
			.maxValue = kMaxGridCount,
			});
		MyGUI::DragInt("Yグリッド数", gridCountY_, {
			.minValue = 1,
			.maxValue = kMaxGridCount,
			});
		MyGUI::DragFloat("グリッド幅", gridWidth_, {
			.dragSpeed = 0.1f,
			.minValue = 0.001f,
			.maxValue = 100000.0f,
			});
		MyGUI::AssetReferenceField(
			"モデル", model_, assetDatabase, { AssetType::Mesh });
	}

	if (MyGUI::CollapsingHeader("ポイントライト設定")) {

		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"PerformanceCheckPointLightSettings");
		MyGUI::Checkbox(
			"グリッド間にポイントライトを配置",
			placePointLights_);
		ImGui::BeginDisabled(!placePointLights_);
		MyGUI::Checkbox("影を有効にする", pointLightShadows_);
		MyGUI::DragInt("配置数", pointLightCount_, {
			.minValue = 1,
			.maxValue = static_cast<int32_t>(kMaxEntityCount),
			});
		MyGUI::DragFloat("強度", pointLightIntensity_, {
			.dragSpeed = 0.01f,
			.minValue = 0.0f,
			.maxValue = 128.0f,
			});
		MyGUI::DragFloat("半径", pointLightRadius_, {
			.dragSpeed = 0.01f,
			.minValue = 0.0f,
			.maxValue = 512.0f,
			});
		MyGUI::DragFloat("減衰", pointLightDecay_, {
			.dragSpeed = 0.01f,
			.minValue = 0.0f,
			.maxValue = 512.0f,
			});
		ImGui::EndDisabled();
	}

	ImGui::Spacing();
	const float buttonSpacing = ImGui::GetStyle().ItemSpacing.x;
	const float buttonWidth =
		(ImGui::GetContentRegionAvail().x - buttonSpacing) * 0.5f;

	ImGui::BeginDisabled(!model_);
	if (ImGui::Button("配置", ImVec2(buttonWidth, 0.0f))) {

		const uint64_t entityCount =
			static_cast<uint64_t>(gridCountXZ_) *
			static_cast<uint64_t>(gridCountXZ_) *
			static_cast<uint64_t>(gridCountY_) +
			static_cast<uint64_t>(CalculatePointLightCount(
				gridCountXZ_, gridCountY_, placePointLights_,
				pointLightCount_));
		if (entityCount > kMaxEntityCount) {

			statusMessage_ = "配置できるエンティティ数は1000000までです";
			statusError_ = true;
		} else {

			std::vector<MeshSubMeshLayoutItem> layout;
			MeshAssetAuthoringInfo meshInfo{};
			if (!MeshSubMeshAuthoring::TryBuildLayout(
				assetDatabase, model_, layout, &meshInfo) ||
				layout.empty()) {

				statusMessage_ = "モデルを解析できません";
				statusError_ = true;
			} else {

				uint64_t vertexCount = 0;
				for (const MeshSubMeshLayoutItem& item : layout) {
					vertexCount += item.vertexCount;
				}
				meshVertexCounts_[model_] = vertexCount;

				if (!sceneState.rootStableUUID) {
					sceneState.rootStableUUID = UUID::New();
				}
				auto command =
					std::make_unique<SetPerformanceGridCommand>(
						sceneState.rootStableUUID, model_,
						gridCountXZ_, gridCountY_, gridWidth_,
						meshInfo.hasBones,
						placePointLights_, pointLightShadows_,
						pointLightCount_,
						pointLightIntensity_,
						pointLightRadius_, pointLightDecay_,
						std::move(layout));
				if (host->ExecuteEditorCommand(std::move(command))) {

					statusMessage_ = "グリッドを配置しました";
					statusError_ = false;
					sceneState.observedRoot = Entity::Null();
				} else {

					statusMessage_ = "グリッドを配置できません";
					statusError_ = true;
				}
			}
		}
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	root = sceneState.rootStableUUID ?
		world->FindByUUID(sceneState.rootStableUUID) : Entity::Null();
	ImGui::BeginDisabled(!world->IsAlive(root));
	if (ImGui::Button("削除", ImVec2(buttonWidth, 0.0f))) {

		if (host->ExecuteEditorCommand(
			std::make_unique<SetPerformanceGridCommand>(
				sceneState.rootStableUUID))) {

			statusMessage_ = "グリッドを削除しました";
			statusError_ = false;
			sceneState.observedRoot = Entity::Null();
			sceneState.drawEntityCount = 0;
			sceneState.pointLightCount = 0;
			sceneState.totalVertexCount = 0;
		}
	}
	ImGui::EndDisabled();

	root = sceneState.rootStableUUID ?
		world->FindByUUID(sceneState.rootStableUUID) : Entity::Null();
	if (root != sceneState.observedRoot) {
		RefreshStatistics(context, sceneState, root);
	}

	ImGui::Separator();
	{
		MyGUI::ScopedPropertyLabelWidth labelWidth(
			"PerformanceCheckStatistics");
		const FrameProfiler& profiler =
			FrameProfiler::GetInstance();
		if (MyGUI::BeginPropertyRow("FPS")) {
			ImGui::Text("%.1f", profiler.GetFps());
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("描画CPU")) {
			ImGui::Text("%.3f ms",
				profiler.GetAverageMs(
					FrameProfiler::Category::Draw));
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("GPU合計")) {
			ImGui::Text("%.3f ms",
				profiler.GetGPUTotalMs());
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("メッシュバッチ更新CPU")) {
			ImGui::Text("%.3f ms",
				profiler.GetAverageMs(
					FrameProfiler::Category::MeshBatchUpload));
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("描画エンティティ数")) {
			ImGui::Text("%llu",
				static_cast<unsigned long long>(
					sceneState.drawEntityCount));
			MyGUI::EndPropertyRow();
		}
		if (sceneState.pointLightCount > 0 &&
			MyGUI::BeginPropertyRow("ライト数")) {

			ImGui::Text("%llu",
				static_cast<unsigned long long>(
					sceneState.pointLightCount));
			MyGUI::EndPropertyRow();
		}
		if (MyGUI::BeginPropertyRow("合計頂点数")) {
			ImGui::Text("%llu",
				static_cast<unsigned long long>(
					sceneState.totalVertexCount));
			MyGUI::EndPropertyRow();
		}
	}

	if (!statusMessage_.empty()) {
		ImGui::Separator();
		if (statusError_) {
			ImGui::TextColored(
				ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
				"%s", statusMessage_.c_str());
		} else {
			ImGui::TextDisabled("%s", statusMessage_.c_str());
		}
	}
	ImGui::End();
}

void Engine::PerformanceCheckTool::RefreshStatistics(
	const EditorToolContext& context,
	GridSceneState& state, const Entity& root) {

	ECSWorld* world = context.GetWorld();
	state.observedRoot = root;
	state.drawEntityCount = 0;
	state.pointLightCount = 0;
	state.totalVertexCount = 0;
	if (!world || !world->IsAlive(root)) {
		return;
	}

	const HierarchyComponent* rootHierarchy =
		world->TryGetComponent<HierarchyComponent>(root);
	if (!rootHierarchy) {
		return;
	}

	Entity child = rootHierarchy->firstChild;
	while (world->IsAlive(child)) {

		const HierarchyComponent* childHierarchy =
			world->TryGetComponent<HierarchyComponent>(child);
		const Entity next = childHierarchy ?
			childHierarchy->nextSibling : Entity::Null();

		const MeshRendererComponent* renderer =
			world->TryGetComponent<MeshRendererComponent>(child);
		if (renderer && renderer->visible) {

			++state.drawEntityCount;
			state.totalVertexCount += ResolveMeshVertexCount(
				context.toolContext.assetDatabase, renderer->mesh);
		}
		if (world->TryGetComponent<PointLightComponent>(child)) {
			++state.pointLightCount;
		}
		child = next;
	}
}

uint64_t Engine::PerformanceCheckTool::ResolveMeshVertexCount(
	AssetDatabase* assetDatabase, AssetID meshAssetID) {

	if (!assetDatabase || !meshAssetID) {
		return 0;
	}

	const auto found = meshVertexCounts_.find(meshAssetID);
	if (found != meshVertexCounts_.end()) {
		return found->second;
	}

	std::vector<MeshSubMeshLayoutItem> layout;
	if (!MeshSubMeshAuthoring::TryBuildLayout(
		assetDatabase, meshAssetID, layout)) {
		return 0;
	}

	uint64_t vertexCount = 0;
	for (const MeshSubMeshLayoutItem& item : layout) {
		vertexCount += item.vertexCount;
	}
	meshVertexCounts_[meshAssetID] = vertexCount;
	return vertexCount;
}

void Engine::PerformanceCheckTool::LoadSettings() {

	const std::filesystem::path path = RuntimePaths::GetUserSettingsPath(
		ConfigPaths::kPerformanceCheckTool);
	if (!JsonAdapter::Check(path)) {
		return;
	}

	const nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return;
	}

	gridCountXZ_ = (std::clamp)(
		data.value("gridCountXZ", gridCountXZ_), 1, kMaxGridCount);
	gridCountY_ = (std::clamp)(
		data.value("gridCountY", gridCountY_), 1, kMaxGridCount);
	gridWidth_ = (std::clamp)(
		data.value("gridWidth", gridWidth_), 0.001f, 100000.0f);
	model_ = ParseAssetID(data, "model");

	placePointLights_ = data.value(
		"placePointLights", placePointLights_);
	pointLightShadows_ = data.value(
		"pointLightShadows", pointLightShadows_);
	pointLightCount_ = (std::clamp)(
		data.value("pointLightCount", pointLightCount_),
		1, static_cast<int32_t>(kMaxEntityCount));
	pointLightIntensity_ = (std::clamp)(
		data.value("pointLightIntensity", pointLightIntensity_),
		0.0f, 128.0f);
	pointLightRadius_ = (std::clamp)(
		data.value("pointLightRadius", pointLightRadius_),
		0.0f, 512.0f);
	pointLightDecay_ = (std::clamp)(
		data.value("pointLightDecay", pointLightDecay_),
		0.0f, 512.0f);
}

void Engine::PerformanceCheckTool::SaveSettings() const {

	nlohmann::json data{};
	data["schemaVersion"] = 2;
	data["gridCountXZ"] = gridCountXZ_;
	data["gridCountY"] = gridCountY_;
	data["gridWidth"] = gridWidth_;
	data["model"] = ToAssetReferenceJson(model_);
	data["placePointLights"] = placePointLights_;
	data["pointLightShadows"] = pointLightShadows_;
	data["pointLightCount"] = pointLightCount_;
	data["pointLightIntensity"] = pointLightIntensity_;
	data["pointLightRadius"] = pointLightRadius_;
	data["pointLightDecay"] = pointLightDecay_;

	JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(
		ConfigPaths::kPerformanceCheckTool), data);
}
