#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Physics/Collision/CollisionQuery.h>
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Rendering/Renderer/Views/GameViewCameraSnapshot.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <algorithm>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	物理クエリのコールバック
	//	レイキャストはCollisionQueryへ委譲し、Entityは世代付きハンドルへ変換して返す
	//============================================================================
	namespace {

		// ManagedVector3からRayを作る、directionはクエリ側で正規化される
		Ray MakeRay(const ManagedVector3& origin, const ManagedVector3& direction) {

			Ray ray{};
			ray.origin = Vector3(origin.x, origin.y, origin.z);
			ray.direction = Vector3(direction.x, direction.y, direction.z);
			return ray;
		}

		// RaycastHit3DをC#へ渡すヒット情報へ変換する
		ManagedRaycastHit ToManagedRaycastHit(ECSWorld& world, const RaycastHit3D& hit) {

			ManagedRaycastHit managed{};
			managed.entity = MakeNativeEntity(world, hit.entity);
			managed.point = ToManagedVector3(hit.point);
			managed.normal = ToManagedVector3(hit.normal);
			managed.distance = hit.distance;
			managed.shapeIndex = hit.shapeIndex;
			managed.triangleIndex = hit.triangleIndex;
			managed.trigger = hit.trigger ? 1 : 0;
			return managed;
		}
	}

	int32_t ManagedScriptRuntime::PhysicsRaycastCallback(ManagedVector3 origin, ManagedVector3 direction,
		float maxDistance, uint32_t layerMask, uint32_t targets, ManagedRaycastHit* outHit) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world || !outHit) {
			return 0;
		}

		RaycastHit3D hit{};
		if (!CollisionQuery::Raycast(*world, MakeRay(origin, direction), maxDistance,
			layerMask, static_cast<RaycastTargets>(targets), hit)) {
			return 0;
		}
		*outHit = ToManagedRaycastHit(*world, hit);
		return 1;
	}

	int32_t ManagedScriptRuntime::PhysicsRaycastAllCallback(ManagedVector3 origin, ManagedVector3 direction,
		float maxDistance, uint32_t layerMask, uint32_t targets, ManagedRaycastHit* buffer, int32_t capacity) {

		const SystemContext* context = GetCurrentContext();
		ECSWorld* world = context ? context->world : nullptr;
		if (!world) {
			return 0;
		}

		std::vector<RaycastHit3D> hits{};
		CollisionQuery::RaycastAll(*world, MakeRay(origin, direction), maxDistance,
			layerMask, static_cast<RaycastTargets>(targets), hits);

		// capacity分だけ書き込み、総数を返してC#側の再確保判断に使う
		if (buffer && 0 < capacity) {
			const int32_t writeCount = (std::min)(static_cast<int32_t>(hits.size()), capacity);
			for (int32_t i = 0; i < writeCount; ++i) {
				buffer[i] = ToManagedRaycastHit(*world, hits[i]);
			}
		}
		return static_cast<int32_t>(hits.size());
	}

	int32_t ManagedScriptRuntime::ScreenPointToRayCallback(float x, float y,
		ManagedVector3* outOrigin, ManagedVector3* outDirection) {

		if (!outOrigin || !outDirection) {
			return 0;
		}
		const GameViewCameraSnapshot::Snapshot& camera = GameViewCameraSnapshot::Get();
		if (!camera.valid || camera.width <= 0.0f || camera.height <= 0.0f) {
			return 0;
		}

		// GameViewピクセル座標をNDCへ写し、near/far点をunprojectして方向を作る
		const float ndcX = (x / camera.width) * 2.0f - 1.0f;
		const float ndcY = 1.0f - (y / camera.height) * 2.0f;
		const Vector3 nearPoint = Vector3::Transform(Vector3(ndcX, ndcY, 0.0f), camera.inverseViewProjection);
		const Vector3 farPoint = Vector3::Transform(Vector3(ndcX, ndcY, 1.0f), camera.inverseViewProjection);

		const Vector3 rayDirection = Vector3::NormalizeOr(farPoint - nearPoint, Vector3::AnyInit(0.0f));
		if (rayDirection.Length() <= 0.0001f) {
			return 0;
		}
		*outOrigin = ToManagedVector3(nearPoint);
		*outDirection = ToManagedVector3(rayDirection);
		return 1;
	}

	int32_t ManagedScriptRuntime::GetMousePositionInViewCallback(ManagedVector2* outPosition) {

		if (!outPosition) {
			return 0;
		}
		Input* input = Input::GetInstance();
		if (!input) {
			return 0;
		}
		const std::optional<Vector2> position = input->GetMousePosInView(InputViewArea::Game);
		if (!position.has_value()) {
			return 0;
		}
		*outPosition = ToManagedVector2(position.value());
		return 1;
	}

	uint32_t ManagedScriptRuntime::GetCollisionTypeMaskByNameCallback(const char* name) {

		if (!name) {
			return 0;
		}
		CollisionSettings& settings = CollisionSettings::GetInstance();
		settings.EnsureLoaded();

		const std::vector<CollisionTypeDefinition>& types = settings.GetTypes();
		for (uint32_t index = 0; index < static_cast<uint32_t>(types.size()); ++index) {
			if (types[index].name == name) {
				return MakeCollisionTypeBit(index);
			}
		}
		return 0;
	}
}
