#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionRaycast.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	// front
	class ECSWorld;

	//============================================================================
	//	CollisionQuery structures
	//============================================================================
	// レイキャストの判定対象
	enum class RaycastTargets : uint32_t {

		None = 0,
		// CollisionComponentの3D形状
		Colliders = 1 << 0,
		All = Colliders,
	};

	bool HasRaycastTarget(RaycastTargets targets, RaycastTargets target);

	// Triggerをクエリへ含める条件
	enum class QueryTriggerInteraction : uint32_t {

		UseGlobal,
		Ignore,
		Collide,
	};

	// 除外対象を除き、線分上で最初に当たる2Dまたは3Dの衝突形状を返す
	bool RaycastBeamSegment(ECSWorld& world, const Vector3& start, const Vector3& end, bool is2D,
		uint32_t layerMask, QueryTriggerInteraction triggerInteraction, std::span<const Entity> excluded, RaycastHit3D& outHit);

	//============================================================================
	//	CollisionQuery class
	//	ワールド内の衝突形状に対するレイクエリ
	//============================================================================
	class CollisionQuery {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CollisionQuery() = default;
		~CollisionQuery() = default;

		// 最近ヒットを返す、ヒット無しはfalse
		static bool Raycast(ECSWorld& world, const Ray& ray, float maxDistance,
			uint32_t layerMask, RaycastTargets targets,
			QueryTriggerInteraction triggerInteraction, RaycastHit3D& outHit);
		// 全ヒットを距離昇順で返す
		static void RaycastAll(ECSWorld& world, const Ray& ray, float maxDistance,
			uint32_t layerMask, RaycastTargets targets,
			QueryTriggerInteraction triggerInteraction, std::vector<RaycastHit3D>& outHits);
	};
} // Engine
