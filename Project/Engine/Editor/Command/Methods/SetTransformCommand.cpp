#include "SetTransformCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Editor/Command/TransformEditUtility.h>

//============================================================================
//	SetTransformCommand classMethods
//============================================================================

Engine::SetTransformCommand::SetTransformCommand(const Entity& targetEntity,
	const TransformComponent& beforeTransform,
	const TransformComponent& afterTransform) :
	initialTarget_(targetEntity),
	beforeTransform_(beforeTransform),
	afterTransform_(afterTransform) {
}

bool Engine::SetTransformCommand::NearlyEqualTransform(const Engine::TransformComponent& lhs,
	const Engine::TransformComponent& rhs) {

	return Engine::Vector3::NearlyEqual(lhs.localPos, rhs.localPos) &&
		Engine::Quaternion::NearlyEqual(lhs.localRotation, rhs.localRotation) &&
		Engine::Vector3::NearlyEqual(lhs.localScale, rhs.localScale);
}

bool Engine::SetTransformCommand::ApplyTransform(EditorCommandContext& context, const TransformComponent& transform) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	// UUIDからエンティティを検索
	Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}
	// トランスフォームをワールドに即座に適用する
	if (!TransformEditUtility::ApplyImmediate(*world, target, transform)) {
		return false;
	}
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetTransformCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回の実行時は対象エンティティのUUIDを取得し、トランスフォームが変更されていない場合はコマンドを実行しない
	if (!targetStableUUID_) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (NearlyEqualTransform(beforeTransform_, afterTransform_)) {
			return false;
		}
	}
	return ApplyTransform(context, afterTransform_);
}

void Engine::SetTransformCommand::Undo(EditorCommandContext& context) {

	ApplyTransform(context, beforeTransform_);
}

bool Engine::SetTransformCommand::Redo(EditorCommandContext& context) {

	return ApplyTransform(context, afterTransform_);
}