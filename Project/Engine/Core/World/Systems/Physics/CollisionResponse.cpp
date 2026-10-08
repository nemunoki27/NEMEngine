#include "CollisionResponse.h"

//============================================================================
//	include
//============================================================================
#include "CollisionBodyUtility.h"
#include "CollisionContactResponse.h"
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>

// c++
#include <algorithm>

using namespace Engine::CollisionBodyUtility;
using namespace Engine::CollisionContactResponse;
using namespace Engine::CollisionFrameBuilder;

namespace {

	// 接地中に許容するめり込み
	constexpr float kPenetrationSlop = 0.001f;
	// 3Dの補正率
	constexpr float kPenetrationCorrectionRate3D = 0.8f;
	// 2Dの補正率
	constexpr float kPenetrationCorrectionRate2D = 1.0f;
	// 拘束方向へ押し戻せるかを判定する下限
	constexpr float kTranslationResponseEpsilon = 0.000001f;

	// 2D形状か
	bool IsShape2D(const Engine::CollisionShapeInstance* shape) {

		return shape && Engine::IsCollisionShape2D(shape->type);
	}
}

void Engine::CollisionResponse::ApplyPushback(
	ECSWorld& world, CollisionRuntimeEntity& a, CollisionRuntimeEntity& b, const CollisionContact& contact) {

	if (contact.trigger || !a.collision || !b.collision) {
		return;
	}

	// 剛体がある組はDynamicだけを押し戻す
	bool movableA;
	bool movableB;
	if (HasRigidbody(world, a.entity) || HasRigidbody(world, b.entity)) {

		movableA = IsDynamicRigidbody(world, a.entity);
		movableB = IsDynamicRigidbody(world, b.entity);
	} else {

		movableA = !a.collision->isStatic && a.collision->enablePushback;
		movableB = !b.collision->isStatic && b.collision->enablePushback;
	}
	if (!movableA && !movableB) {
		return;
	}

	const CollisionShapeInstance* shapeA = a.hasShape ? &a.shape : nullptr;
	const CollisionShapeInstance* shapeB = b.hasShape ? &b.shape : nullptr;
	const bool resolveAs2D = IsShape2D(shapeA) && IsShape2D(shapeB);

	// 固定軸を除いた法線成分で、めり込みを解消できる側へ押し戻し量を配分する
	const Vector3 responseDirectionA =
		movableA ? ApplyTranslationConstraints(world, a.entity, contact.normal) : Vector3::AnyInit(0.0f);
	const Vector3 responseDirectionB =
		movableB ? ApplyTranslationConstraints(world, b.entity, contact.normal) : Vector3::AnyInit(0.0f);
	const float responseA =
		(std::max)(Vector3::Dot(responseDirectionA, contact.normal), 0.0f) * ResolveInverseMass(world, a.entity);
	const float responseB =
		(std::max)(Vector3::Dot(responseDirectionB, contact.normal), 0.0f) * ResolveInverseMass(world, b.entity);
	const float responseSum = responseA + responseB;
	const float correctionDepth = (std::max)(contact.penetration - kPenetrationSlop, 0.0f);
	bool movedA = false;
	bool movedB = false;
	if (kTranslationResponseEpsilon < responseSum && 0.0f < correctionDepth) {

		const float correctionRate = resolveAs2D ? kPenetrationCorrectionRate2D : kPenetrationCorrectionRate3D;
		const float correctionScale = correctionDepth * correctionRate / responseSum;
		if (movableA) {
			MoveEntity(world, a.entity, -responseDirectionA * correctionScale);
			movedA = true;
		}
		if (movableB) {
			MoveEntity(world, b.entity, responseDirectionB * correctionScale);
			movedB = true;
		}
	}

	const bool pairResolved =
		movableA && movableB &&
		ResolvePairContactVelocity(world, a.entity, b.entity, -contact.normal, contact.point, shapeA, shapeB);
	if (movableA && !pairResolved) {
		ResolveContactVelocity(world, a.entity, -contact.normal, contact.point, shapeA, shapeB);
	}
	if (movableB && !pairResolved) {
		ResolveContactVelocity(world, b.entity, contact.normal, contact.point, shapeB, shapeA);
	}

	// 隣接Colliderを続けて解く場合も、補正前の形状で二重に押し戻さない
	if (movedA) {
		RebuildRuntimeShape(world, a);
	}
	if (movedB) {
		RebuildRuntimeShape(world, b);
	}
}

void Engine::CollisionResponse::MoveByWorldDelta(ECSWorld& world, CollisionRuntimeEntity& runtime, const Vector3& delta) {

	MoveEntity(world, runtime.entity, delta);
	RebuildRuntimeShape(world, runtime);
}
