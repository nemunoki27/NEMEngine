#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/World/Components/Rendering/BillboardComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

namespace Engine::RenderBillboard {

	inline bool HasBillboard(const RenderItem& item) {

		return item.world && item.world->IsAlive(item.entity) &&
			item.world->HasComponent<BillboardComponent>(item.entity);
	}

	Matrix4x4 ResolveParentWorldMatrix(ECSWorld& world, const Entity& entity);
	Quaternion QuaternionFromRotationMatrix(const Matrix4x4& matrix);
	Quaternion MakeLookAtBillboardRotation(const Vector3& objectWorldPos, const Vector3& cameraWorldPos);
	Quaternion MakeCameraBillboardRotation(const ResolvedCameraView& camera, const Vector3& objectWorldPos);
	Quaternion ApplyAxisMask(const Quaternion& currentLocal, const Quaternion& desiredLocal, const BillboardComponent& billboard, const Vector3& localForward);
	Matrix4x4 ResolveWorldMatrix(const RenderItem& item, const ResolvedRenderView& view);
	Quaternion ExtractRotationNoScale(const Matrix4x4& matrix);		

} // namespace Engine::RenderBillboard