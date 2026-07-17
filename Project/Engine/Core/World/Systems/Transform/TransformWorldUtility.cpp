#include "TransformWorldUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Systems/Animation/JointAttachmentUtility.h>
#include <Engine/Core/Foundation/Math/AffineDecompose.h>

// c++
#include <algorithm>
#include <vector>

namespace {

	using namespace Engine;

	struct ResolveContext {

		std::vector<Entity> stack;
	};

	Vector3 MultiplyScale(const Vector3& lhs, const Vector3& rhs) {
		return Vector3(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z);
	}

	bool ResolveWorldTransformInternal(ECSWorld& world, const Entity& entity,
		ResolvedWorldTransform& outTransform, ResolveContext& context);

	bool ResolveParentSourceTransform(ECSWorld& world, const Entity& entity,
		ResolvedWorldTransform& outTransform, ResolveContext& context) {

		Entity skinnedEntity = Entity::Null();
		Matrix4x4 jointSkeletonSpace{};
		if (JointAttachmentUtility::ResolveAttachedJoint(
			world, entity, skinnedEntity, jointSkeletonSpace)) {

			ResolvedWorldTransform skinnedTransform{};
			if (ResolveWorldTransformInternal(world, skinnedEntity, skinnedTransform, context)) {

				outTransform.matrix = jointSkeletonSpace * skinnedTransform.matrix;
				Vector3 position{};
				outTransform.rotation = skinnedTransform.rotation;
				outTransform.scale = skinnedTransform.scale;
				DecomposeAffine3D(outTransform.matrix, position, outTransform.rotation, outTransform.scale);
				return true;
			}
		}

		const HierarchyComponent* hierarchy = world.TryGetComponent<HierarchyComponent>(entity);
		if (!hierarchy || !world.IsAlive(hierarchy->parent)) {
			return false;
		}
		return ResolveWorldTransformInternal(world, hierarchy->parent, outTransform, context);
	}

	bool ResolveParentFollowTransformInternal(ECSWorld& world, const Entity& entity,
		ResolvedWorldTransform& outTransform, ResolveContext& context) {

		const TransformComponent* transform = world.TryGetComponent<TransformComponent>(entity);
		if (!transform) {
			return false;
		}

		ResolvedWorldTransform parentTransform{};
		if (!ResolveParentSourceTransform(world, entity, parentTransform, context)) {
			outTransform = {};
			return true;
		}

		outTransform.matrix = BuildParentFollowMatrix(parentTransform.matrix,
			transform->ignoreParentScale, transform->ignoreParentRotation);
		outTransform.rotation = transform->ignoreParentRotation ?
			Quaternion::Identity() : parentTransform.rotation;
		outTransform.scale = transform->ignoreParentScale ?
			Vector3::AnyInit(1.0f) : parentTransform.scale;
		return true;
	}

	bool ResolveWorldTransformInternal(ECSWorld& world, const Entity& entity,
		ResolvedWorldTransform& outTransform, ResolveContext& context) {

		if (!world.IsAlive(entity)) {
			return false;
		}
		const TransformComponent* transform = world.TryGetComponent<TransformComponent>(entity);
		if (!transform) {
			return false;
		}
		if (std::find(context.stack.begin(), context.stack.end(), entity) != context.stack.end()) {
			return false;
		}

		context.stack.emplace_back(entity);

		ResolvedWorldTransform parentFollow{};
		const bool resolvedParent = ResolveParentFollowTransformInternal(world, entity, parentFollow, context);
		if (resolvedParent) {

			outTransform.matrix = MakeLocalMatrix(*transform) * parentFollow.matrix;
			outTransform.rotation = Quaternion::Normalize(parentFollow.rotation * transform->localRotation);
			outTransform.scale = MultiplyScale(transform->localScale, parentFollow.scale);
		}

		context.stack.pop_back();
		return resolvedParent;
	}
}

//============================================================================
//	TransformWorldUtility methods
//============================================================================
bool Engine::TransformWorldUtility::ResolveWorldTransform(ECSWorld& world, const Entity& entity,
	ResolvedWorldTransform& outTransform) {

	ResolveContext context{};
	return ResolveWorldTransformInternal(world, entity, outTransform, context);
}

bool Engine::TransformWorldUtility::ResolveParentFollowTransform(ECSWorld& world, const Entity& entity,
	ResolvedWorldTransform& outTransform) {

	if (!world.IsAlive(entity)) {
		return false;
	}
	ResolveContext context{};
	context.stack.emplace_back(entity);
	return ResolveParentFollowTransformInternal(world, entity, outTransform, context);
}
