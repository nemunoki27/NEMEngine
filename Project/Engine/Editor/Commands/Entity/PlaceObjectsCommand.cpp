#include "PlaceObjectsCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/EditorEntityDuplicateUtility.h>
#include <Engine/Editor/Utility/JointAttachmentEditor.h>
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotDuplicator.h>
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotPlacement.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

using namespace Engine;

PlaceObjectsCommand::PlaceObjectsCommand(LogicPlacementRequest request) : request_(std::move(request)) {
}

Quaternion PlaceObjectsCommand::ResolveRotation(const LogicPlacementPoint& point, Vector3 normal, Quaternion original) const {

	if (request_.orientation == LogicPlacementOrientation::Original) return original;
	const auto direction = request_.orientation == LogicPlacementOrientation::Center ? -point.position : point.direction;
	if (direction.Length() < 0.00001f) return original;
	const std::array<Vector3, 6> axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}}};
	const auto forward = axes[static_cast<size_t>(request_.forward)];
	Quaternion rotation;
	if (request_.settings.is2D) {
		rotation = Quaternion::MakeAxisAngle({0, 0, 1}, std::atan2(direction.y, direction.x) - std::atan2(forward.y, forward.x));
	} else {
		const auto up = std::abs(forward.y) > 0.5f ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
		rotation = Quaternion::LookRotation(direction.Normalize(), normal) * Quaternion::Inverse(Quaternion::LookRotation(forward, up));
	}
	return (rotation * Quaternion::FromEulerDegrees(request_.rotationOffset)).Normalize();
}

bool PlaceObjectsCommand::Execute(EditorCommandContext& context) {

	auto* world = context.GetWorld();
	if (!world || !context.CanEditScene() || rootID_) return false;
	const auto layout = BuildLogicPlacement(request_.settings);
	if (!layout.IsValid() || request_.orientation > LogicPlacementOrientation::Center ||
		request_.forward > LogicPlacementForwardAxis::NegativeZ) return false;
	if (request_.settings.is2D && request_.orientation != LogicPlacementOrientation::Original &&
		(request_.forward == LogicPlacementForwardAxis::PositiveZ || request_.forward == LogicPlacementForwardAxis::NegativeZ)) return false;
	const auto finite = [](Vector3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); };
	const auto& parentValue = request_.parentTransform;
	if (!finite(parentValue.localPos) || !finite(parentValue.localScale) || !finite(request_.rotationOffset) ||
		!std::isfinite(parentValue.localRotation.Length()) || parentValue.localRotation.Length() < 0.00001f) return false;
	TransformComponent sourceTransform;
	std::string diagnostic;
	auto* database = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	if (!ResolveLogicPlacementSource(*world, database, request_.source, request_.settings.is2D, sourceTransform, diagnostic)) return false;
	EditorEntityTreeSnapshot source;
	if (request_.source.object) EditorEntitySnapshotUtility::CaptureSubtree(*world, world->FindByUUID(request_.source.object), source);
	if (request_.source.object && source.IsEmpty()) return false;
	previousSelection_.Capture(context);
	SceneCreationScope creation(*world);
	try {
		std::string name = request_.parentName;
		if (name.find_first_not_of(" \t\r\n") == std::string::npos) name = "Object";
		const auto parent = SceneAuthoring::CreateGameObject(*world, SceneAuthoring::MakeUniqueEntityName(*world, name));
		const UUID parentID = world->GetUUID(parent);
		auto& parentTransform = world->GetComponent<TransformComponent>(parent);
		parentTransform = parentValue;
		parentTransform.localRotation = parentTransform.localRotation.Normalize();
		parentTransform.dimension = request_.settings.is2D ? Dimension::Type2D : Dimension::Type3D;
		parentTransform.isDirty = true;
		auto& membership = world->GetComponent<SceneObjectComponent>(parent);
		membership.sceneInstanceID = context.editorContext->activeSceneInstanceID;
		membership.sourceAsset = context.editorContext->activeSceneAsset;
		const EntitySnapshotScene destination{membership.sceneInstanceID, membership.sourceAsset};
		if (!HierarchyUtility::TryGetNextRootSiblingOrder(*world, parent,
			world->GetComponent<HierarchyComponent>(parent).siblingOrder)) return false;
		HierarchySystem hierarchy;
		PrefabSystem prefabs;
		for (size_t index = 0; index < layout.points.size(); ++index) {
			Entity object;
			if (request_.source.prefab) {
				PrefabInstantiateDesc desc{};
				desc.parent = parent;
				desc.ownerSceneInstanceID = destination.instanceID;
				desc.renameRootToPrefabName = true;
				PrefabInstantiateResult generated;
				if (!prefabs.InstantiatePrefab(*database, hierarchy, *world, request_.source.prefab, generated, desc))
					throw std::runtime_error("配置するPrefabを生成できません");
				object = generated.root;
			} else {
				EditorEntityTreeSnapshot copy;
				const auto& root = *std::find_if(source.entities.begin(), source.entities.end(),
					[&](const auto& value) { return value.stableUUID == source.rootStableUUID; });
				const std::string sourceName = root.components.contains("Name") ? root.components["Name"].value("name", "GameObject") : "GameObject";
				EntitySnapshotDuplicator::Build(source, sourceName, copy);
				EntitySnapshotDuplicator::ClearRootParentLink(copy);
				EntitySnapshotPlacement::Apply(std::span(&copy, 1), std::span(&destination, 1));
				object = EditorEntityDuplicateUtility::InstantiatePreparedSnapshot(*world, copy, parentID);
				if (!world->IsAlive(object)) throw std::runtime_error("配置するオブジェクトを複製できません");
				const auto restored = EditorEntitySnapshotUtility::CollectSubtreeEntities(*world, object);
				EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(context, *world, copy, restored);
			}
			if (!world->IsAlive(object)) throw std::runtime_error("配置したオブジェクトが見つかりません");
			JointAttachmentEditor::Detach(*world, object);
			auto* transform = world->TryGetComponent<TransformComponent>(object);
			if (!transform) throw std::runtime_error("配置元にTransformがありません");
			transform->localPos = layout.points[index].position;
			transform->localRotation = ResolveRotation(layout.points[index], layout.normal, sourceTransform.localRotation);
			transform->localScale = sourceTransform.localScale;
			transform->ignoreParentScale = false;
			transform->ignoreParentRotation = false;
			transform->isDirty = true;
			world->MarkComponentModified<TransformComponent>(object);
		}
		MarkTransformSubtreeDirty(*world, parent);
		context.RebuildHierarchyAll();
		if (context.editorState) context.editorState->SelectEntity(parent);
		rootID_ = parentID;
		lifetime_ = world->GetLifetime();
		creation.Commit();
		return true;
	} catch (...) {
		previousSelection_.Restore(context);
		throw;
	}
}

void PlaceObjectsCommand::Undo(EditorCommandContext& context) {

	auto* world = context.GetWorld();
	if (!context.CanEditScene() || !world || world->GetLifetime() != lifetime_.lock() || !world->IsAlive(world->FindByUUID(rootID_)))
		throw std::runtime_error("配置を取り消せるWorldまたはオブジェクトがありません");
	auto removal = std::make_unique<DeleteEntityCommand>(world->FindByUUID(rootID_));
	if (!removal->Execute(context)) throw std::runtime_error("配置したオブジェクトを削除できません");
	previousSelection_.Restore(context);
	removal_ = std::move(removal);
}

bool PlaceObjectsCommand::Redo(EditorCommandContext& context) {

	auto* world = context.GetWorld();
	if (!context.CanEditScene() || !world || world->GetLifetime() != lifetime_.lock() || !removal_) return false;
	removal_->Undo(context);
	if (!world->IsAlive(world->FindByUUID(rootID_))) return false;
	if (context.editorState) context.editorState->SelectEntity(world->FindByUUID(rootID_));
	removal_.reset();
	return true;
}
