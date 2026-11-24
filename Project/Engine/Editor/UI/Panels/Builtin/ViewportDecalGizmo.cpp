#include "ViewportDecalGizmo.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DebugDraw/Lines/Dimensions/LineRenderer3D.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Decals/DecalProjection.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

void Engine::ViewportDecalGizmo::Draw(ECSWorld& world, const Entity& entity, LineRenderer3D& lines) {

	const auto* projector = world.TryGetComponent<DecalProjectorComponent>(entity);
	const auto* transform = world.TryGetComponent<TransformComponent>(entity);
	if (!projector || !transform) return;
	DecalProjection projection;
	if (!DecalProjectionUtility::Build(*projector, transform->worldMatrix, projection)) return;
	Vector3 points[8];
	for (uint32_t i = 0; i < 8; ++i)
		points[i] = Vector3::Transform({i & 1u ? 0.5f : -0.5f, i & 2u ? 0.5f : -0.5f, i & 4u ? 0.5f : -0.5f}, projection.decalToWorld);
	for (uint32_t i = 0; i < 8; ++i) {
		for (uint32_t axis = 0; axis < 3; ++axis) {
			const auto next = i ^ (1u << axis);
			if (i < next) lines.DrawLine(points[i], points[next], Color4::FromHex(0xffb441e6), 1.5f);
		}
	}
	const auto tip = Vector3::Transform({0, 0, 0.8f}, projection.decalToWorld);
	for (const Vector3 base : {Vector3(0, 0, -0.5f), Vector3(-0.1f, 0, 0.6f), Vector3(0.1f, 0, 0.6f)})
		lines.DrawLine(Vector3::Transform(base, projection.decalToWorld), tip, Color4::FromHex(0x50b4ffff), 2);
}
