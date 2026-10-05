#pragma once

//============================================================================
//	include
//============================================================================
#include "CollisionDetection.h"

namespace Engine::CollisionDetectionDetail {

	// 接触判定で共有する許容誤差
	inline constexpr float kEpsilon = 0.0001f;

	// 内積の絶対値を求める
	float AbsDot(const Engine::Vector3& a, const Engine::Vector3& b);
	// 形状間の方向を求める
	Engine::Vector3 DirectionAToB(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b);
	// 箱上の最近点を求める
	Engine::Vector3 ClosestPointOnBox(const Engine::CollisionShapeInstance& box, const Engine::Vector3& point);
	// 線分上の最近点を求める
	Engine::Vector3 ClosestPointOnSegment(
		const Engine::Vector3& start, const Engine::Vector3& end, const Engine::Vector3& point);
	// 線分同士の最近点を求める
	void ClosestPointsOnSegments(const Engine::Vector3& startA, const Engine::Vector3& endA, const Engine::Vector3& startB,
		const Engine::Vector3& endB, Engine::Vector3& outPointA, Engine::Vector3& outPointB);
	// 線分と箱の最近点を求める
	void ClosestPointsSegmentBox(const Engine::Vector3& start, const Engine::Vector3& end,
		const Engine::CollisionShapeInstance& box, uint32_t dimensionCount, Engine::Vector3& outSegmentPoint,
		Engine::Vector3& outBoxPoint);
	// 接触情報を設定する
	void FillContact(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b,
		Engine::CollisionContact& outContact, const Engine::Vector3& normal, float penetration, const Engine::Vector3& point);
	// 平面判定用にZ成分を除く
	Engine::Vector3 Flatten2D(Engine::Vector3 value);
	// カプセルと球の接触を判定する
	bool TestCapsuleSphere(const Engine::CollisionShapeInstance& capsule, const Engine::CollisionShapeInstance& sphere,
		bool capsuleIsA, bool is2D, Engine::CollisionContact& outContact);
	// カプセル同士の接触を判定する
	bool TestCapsuleCapsule(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b, bool is2D,
		Engine::CollisionContact& outContact);
	// カプセルと箱の接触を判定する
	bool TestCapsuleBox(const Engine::CollisionShapeInstance& capsule, const Engine::CollisionShapeInstance& box,
		bool capsuleIsA, uint32_t dimensionCount, Engine::CollisionContact& outContact);
	// 円同士の接触を判定する
	bool TestCircleCircle(
		const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b, Engine::CollisionContact& outContact);
	// 矩形同士の接触を判定する
	bool TestQuadQuad2D(
		const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b, Engine::CollisionContact& outContact);
	// 円と矩形の接触を判定する
	bool TestCircleQuad2D(const Engine::CollisionShapeInstance& circle, const Engine::CollisionShapeInstance& quad,
		bool circleIsA, Engine::CollisionContact& outContact);
	// 球同士の接触を判定する
	bool TestSphereSphere(
		const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b, Engine::CollisionContact& outContact);
	// 球と箱の接触を判定する
	bool TestSphereBox(const Engine::CollisionShapeInstance& sphere, const Engine::CollisionShapeInstance& box, bool sphereIsA,
		Engine::CollisionContact& outContact);
	// 箱同士の接触を判定する
	bool TestBoxBox3D(const Engine::CollisionShapeInstance& a, const Engine::CollisionShapeInstance& b,
		Engine::CollisionContact& outContact, uint8_t facesA = 0, uint8_t facesB = 0);
}
