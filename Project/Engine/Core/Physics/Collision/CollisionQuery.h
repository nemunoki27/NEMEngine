#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionRaycast.h>

// c++
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
		// FillMeshRendererComponentの三角形
		FillMeshes = 1 << 1,
		All = (1 << 0) | (1 << 1),
	};

	inline bool HasRaycastTarget(RaycastTargets targets, RaycastTargets target) {

		return (static_cast<uint32_t>(targets) & static_cast<uint32_t>(target)) != 0;
	}

	//============================================================================
	//	CollisionQuery class
	//	ワールド内の衝突形状とFillMeshに対するレイクエリ
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
			uint32_t layerMask, RaycastTargets targets, RaycastHit3D& outHit);
		// 全ヒットを距離昇順で返す、コライダーは形状単位でFillMeshはEntity単位で1件
		static void RaycastAll(ECSWorld& world, const Ray& ray, float maxDistance,
			uint32_t layerMask, RaycastTargets targets, std::vector<RaycastHit3D>& outHits);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// CollisionComponentの3D形状のヒットを集める
		static void RaycastColliders(ECSWorld& world, const Ray& ray, float maxDistance,
			uint32_t layerMask, std::vector<RaycastHit3D>& outHits);
		// FillMeshの三角形のヒットを集める、Entityごとに最近三角形の1件
		static void RaycastFillMeshes(ECSWorld& world, const Ray& ray, float maxDistance,
			uint32_t layerMask, std::vector<RaycastHit3D>& outHits);
	};
} // Engine
