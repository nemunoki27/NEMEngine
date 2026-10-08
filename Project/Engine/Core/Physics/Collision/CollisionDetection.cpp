#include "CollisionDetection.h"

//============================================================================
//	include
//============================================================================
#include "CollisionDetectionDetail.h"

using namespace Engine::CollisionDetectionDetail;

bool Engine::IsCollisionShape2D(ColliderShapeType type) {

	return type == ColliderShapeType::Circle2D || type == ColliderShapeType::Quad2D || type == ColliderShapeType::Capsule2D;
}

bool Engine::IsCollisionShape3D(ColliderShapeType type) {

	return type == ColliderShapeType::Sphere3D || type == ColliderShapeType::AABB3D || type == ColliderShapeType::OBB3D ||
		   type == ColliderShapeType::Capsule3D;
}

bool Engine::TestCollision(const CollisionShapeInstance& a, const CollisionShapeInstance& b, CollisionContact& outContact) {

	// 2Dと3Dは別空間として扱い、混在判定は行わない
	if (IsCollisionShape2D(a.type) != IsCollisionShape2D(b.type)) {
		return false;
	}

	if (a.type == ColliderShapeType::Circle2D && b.type == ColliderShapeType::Circle2D) {
		return TestCircleCircle(a, b, outContact);
	}
	if (a.type == ColliderShapeType::Circle2D && b.type == ColliderShapeType::Quad2D) {
		return TestCircleQuad2D(a, b, true, outContact);
	}
	if (a.type == ColliderShapeType::Quad2D && b.type == ColliderShapeType::Circle2D) {
		return TestCircleQuad2D(b, a, false, outContact);
	}
	if (a.type == ColliderShapeType::Quad2D && b.type == ColliderShapeType::Quad2D) {
		return TestQuadQuad2D(a, b, outContact);
	}
	if (a.type == ColliderShapeType::Capsule2D && b.type == ColliderShapeType::Circle2D) {
		return TestCapsuleSphere(a, b, true, true, outContact);
	}
	if (a.type == ColliderShapeType::Circle2D && b.type == ColliderShapeType::Capsule2D) {
		return TestCapsuleSphere(b, a, false, true, outContact);
	}
	if (a.type == ColliderShapeType::Capsule2D && b.type == ColliderShapeType::Quad2D) {
		return TestCapsuleBox(a, b, true, 2, outContact);
	}
	if (a.type == ColliderShapeType::Quad2D && b.type == ColliderShapeType::Capsule2D) {
		return TestCapsuleBox(b, a, false, 2, outContact);
	}
	if (a.type == ColliderShapeType::Capsule2D && b.type == ColliderShapeType::Capsule2D) {
		return TestCapsuleCapsule(a, b, true, outContact);
	}

	if (a.type == ColliderShapeType::Sphere3D && b.type == ColliderShapeType::Sphere3D) {
		return TestSphereSphere(a, b, outContact);
	}
	if (a.type == ColliderShapeType::Sphere3D && (b.type == ColliderShapeType::AABB3D || b.type == ColliderShapeType::OBB3D)) {
		return TestSphereBox(a, b, true, outContact);
	}
	if ((a.type == ColliderShapeType::AABB3D || a.type == ColliderShapeType::OBB3D) && b.type == ColliderShapeType::Sphere3D) {
		return TestSphereBox(b, a, false, outContact);
	}
	if ((a.type == ColliderShapeType::AABB3D || a.type == ColliderShapeType::OBB3D) &&
		(b.type == ColliderShapeType::AABB3D || b.type == ColliderShapeType::OBB3D)) {
		return TestBoxBox3D(a, b, outContact);
	}
	if (a.type == ColliderShapeType::Capsule3D && b.type == ColliderShapeType::Sphere3D) {
		return TestCapsuleSphere(a, b, true, false, outContact);
	}
	if (a.type == ColliderShapeType::Sphere3D && b.type == ColliderShapeType::Capsule3D) {
		return TestCapsuleSphere(b, a, false, false, outContact);
	}
	if (a.type == ColliderShapeType::Capsule3D && (b.type == ColliderShapeType::AABB3D || b.type == ColliderShapeType::OBB3D)) {
		return TestCapsuleBox(a, b, true, 3, outContact);
	}
	if ((a.type == ColliderShapeType::AABB3D || a.type == ColliderShapeType::OBB3D) && b.type == ColliderShapeType::Capsule3D) {
		return TestCapsuleBox(b, a, false, 3, outContact);
	}
	if (a.type == ColliderShapeType::Capsule3D && b.type == ColliderShapeType::Capsule3D) {
		return TestCapsuleCapsule(a, b, false, outContact);
	}
	return false;
}
