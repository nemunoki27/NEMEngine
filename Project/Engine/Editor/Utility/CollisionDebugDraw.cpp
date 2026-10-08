#include "CollisionDebugDraw.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <algorithm>
#include <cmath>

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>

namespace {

	// Triggerは黄色、通常形状はシアンで表示する
	Engine::Color4 GetShapeColor(const Engine::CollisionShapeInstance& shape) {

		return shape.trigger ? Engine::Color4::Yellow(1.0f) : Engine::Color4::Cyan(1.0f);
	}

	// Circle2DをXY平面に描画する
	void DrawCircle2D(const Engine::CollisionShapeInstance& shape, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer2D* renderer = Engine::LineRenderer::GetInstance()->Get2D();
		if (!renderer) {
			return;
		}

		const float radius = shape.radius;
		renderer->DrawCircle(Engine::Vector2(shape.center.x, shape.center.y), radius, color, 16, thickness);
	}

	// Quad2DをXY平面に描画する
	void DrawQuad2D(const Engine::CollisionShapeInstance& shape, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer2D* renderer = Engine::LineRenderer::GetInstance()->Get2D();
		if (!renderer) {
			return;
		}

		// 判定用の軸と半幅から外周を描く
		const Engine::Vector2 center(shape.center.x, shape.center.y);
		const Engine::Vector2 x(shape.axes[0].x * shape.halfExtents.x, shape.axes[0].y * shape.halfExtents.x);
		const Engine::Vector2 y(shape.axes[1].x * shape.halfExtents.y, shape.axes[1].y * shape.halfExtents.y);
		renderer->DrawLine(center - x - y, center + x - y, color, thickness);
		renderer->DrawLine(center + x - y, center + x + y, color, thickness);
		renderer->DrawLine(center + x + y, center - x + y, color, thickness);
		renderer->DrawLine(center - x + y, center - x - y, color, thickness);
	}

	// Capsule2DをXY平面に描画する
	void DrawCapsule2D(const Engine::CollisionShapeInstance& shape, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer2D* renderer = Engine::LineRenderer::GetInstance()->Get2D();
		if (!renderer) {
			return;
		}

		const Engine::CollisionShapeInstance& capsule = shape;
		const Engine::Vector2 start(capsule.segmentStart.x, capsule.segmentStart.y);
		const Engine::Vector2 end(capsule.segmentEnd.x, capsule.segmentEnd.y);
		const Engine::Vector2 segment = end - start;
		const float segmentLength = segment.Length();
		constexpr uint32_t kArcDivision = 16;
		if (segmentLength <= 0.0001f) {
			renderer->DrawCircle(start, capsule.radius, color, kArcDivision * 2, thickness);
			return;
		}

		const Engine::Vector2 axis = segment / segmentLength;
		const Engine::Vector2 perpendicular(-axis.y, axis.x);
		renderer->DrawLine(start + perpendicular * capsule.radius, end + perpendicular * capsule.radius, color, thickness);
		renderer->DrawLine(start - perpendicular * capsule.radius, end - perpendicular * capsule.radius, color, thickness);

		for (uint32_t i = 0; i < kArcDivision; ++i) {

			const float angle0 = Math::pi * static_cast<float>(i) / static_cast<float>(kArcDivision);
			const float angle1 = Math::pi * static_cast<float>(i + 1) / static_cast<float>(kArcDivision);
			const Engine::Vector2 start0 =
				start + (perpendicular * std::cos(angle0) - axis * std::sin(angle0)) * capsule.radius;
			const Engine::Vector2 start1 =
				start + (perpendicular * std::cos(angle1) - axis * std::sin(angle1)) * capsule.radius;
			const Engine::Vector2 end0 = end + (-perpendicular * std::cos(angle0) + axis * std::sin(angle0)) * capsule.radius;
			const Engine::Vector2 end1 = end + (-perpendicular * std::cos(angle1) + axis * std::sin(angle1)) * capsule.radius;
			renderer->DrawLine(start0, start1, color, thickness);
			renderer->DrawLine(end0, end1, color, thickness);
		}
	}

	// Sphere3Dを描画する
	void DrawSphere3D(const Engine::CollisionShapeInstance& shape, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer3D* renderer = Engine::LineRenderer::GetInstance()->Get3D();
		if (!renderer) {
			return;
		}

		const float radius = shape.radius;
		renderer->DrawSphere(shape.center, radius, color, thickness);
	}

	// Capsule3Dを描画する
	void DrawCapsule3D(const Engine::CollisionShapeInstance& shape, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer3D* renderer = Engine::LineRenderer::GetInstance()->Get3D();
		if (!renderer) {
			return;
		}

		const Engine::CollisionShapeInstance& capsule = shape;
		renderer->DrawSphere(capsule.segmentStart, capsule.radius, color, thickness);
		if ((capsule.segmentEnd - capsule.segmentStart).Length() <= 0.0001f) {
			return;
		}
		renderer->DrawSphere(capsule.segmentEnd, capsule.radius, color, thickness);

		const Engine::Vector3 axis =
			Engine::Vector3::NormalizeOr(capsule.segmentEnd - capsule.segmentStart, Engine::Vector3(0.0f, 1.0f, 0.0f));
		const Engine::Vector3 reference =
			std::fabs(axis.y) < 0.99f ? Engine::Vector3(0.0f, 1.0f, 0.0f) : Engine::Vector3(1.0f, 0.0f, 0.0f);
		const Engine::Vector3 right =
			Engine::Vector3::NormalizeOr(Engine::Vector3::Cross(axis, reference), Engine::Vector3(1.0f, 0.0f, 0.0f));
		const Engine::Vector3 forward =
			Engine::Vector3::NormalizeOr(Engine::Vector3::Cross(axis, right), Engine::Vector3(0.0f, 0.0f, 1.0f));
		for (const Engine::Vector3& direction : {right, -right, forward, -forward}) {
			renderer->DrawLine(capsule.segmentStart + direction * capsule.radius,
				capsule.segmentEnd + direction * capsule.radius, color, thickness);
		}
	}

	// AABB3Dを描画する
	void DrawAABB3D(const Engine::CollisionShapeInstance& shape, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer3D* renderer = Engine::LineRenderer::GetInstance()->Get3D();
		if (!renderer) {
			return;
		}

		const Engine::Vector3 center = shape.center;
		const Engine::Vector3 halfExtents = shape.halfExtents;
		renderer->DrawAABB(center - halfExtents, center + halfExtents, color, thickness);
	}

	// OBB3Dを描画する
	void DrawOBB3D(const Engine::CollisionShapeInstance& shape, const Engine::Color4& color, float thickness) {

		Engine::LineRenderer3D* renderer = Engine::LineRenderer::GetInstance()->Get3D();
		if (!renderer) {
			return;
		}

		const Engine::Vector3 halfExtents = shape.halfExtents;
		// 判定用の3軸を描画行列へ渡す
		Engine::Matrix4x4 rotation = Engine::Matrix4x4::Identity();
		for (uint32_t i = 0; i < 3; ++i) {
			rotation.m[i][0] = shape.axes[i].x;
			rotation.m[i][1] = shape.axes[i].y;
			rotation.m[i][2] = shape.axes[i].z;
		}
		renderer->DrawOBB(shape.center, halfExtents, rotation, color, thickness);
	}

	// 形状タイプごとの描画関数へ振り分ける、collidingなら衝突中として赤で描く
	void DrawCollisionShape(const Engine::CollisionShapeInstance& shape, bool colliding) {

		// 衝突中は形状種別に関わらず赤、それ以外はTrigger黄/通常シアン
		const Engine::Color4 color = colliding ? Engine::Color4::Red(1.0f) : GetShapeColor(shape);
		const float thickness = 2.0f;
		switch (shape.type) {
		case Engine::ColliderShapeType::Circle2D:
			DrawCircle2D(shape, color, thickness);
			break;
		case Engine::ColliderShapeType::Quad2D:
			DrawQuad2D(shape, color, thickness);
			break;
		case Engine::ColliderShapeType::Capsule2D:
			DrawCapsule2D(shape, color, thickness);
			break;
		case Engine::ColliderShapeType::Sphere3D:
		case Engine::ColliderShapeType::AABB3D:
		case Engine::ColliderShapeType::OBB3D:
		case Engine::ColliderShapeType::Capsule3D: {

			// 3D形状は不透明メッシュに隠れるよう深度オクルージョン対象バッチへ積む
			Engine::LineRenderer3D* renderer = Engine::LineRenderer::GetInstance()->Get3D();
			if (renderer) {
				renderer->SetOccludedMode(true);
			}
			if (shape.type == Engine::ColliderShapeType::Sphere3D) {
				DrawSphere3D(shape, color, thickness);
			} else if (shape.type == Engine::ColliderShapeType::AABB3D) {
				DrawAABB3D(shape, color, thickness);
			} else if (shape.type == Engine::ColliderShapeType::Capsule3D) {
				DrawCapsule3D(shape, color, thickness);
			} else {
				DrawOBB3D(shape, color, thickness);
			}
			if (renderer) {
				renderer->SetOccludedMode(false);
			}
			break;
		}
		default:
			break;
		}
	}
}
#endif

void Engine::CollisionDebugDraw::DrawWorld([[maybe_unused]] ECSWorld& world) {

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	// World内の有効なCollision形状をすべて描画する
	world.ForEach<CollisionComponent, TransformComponent>([&world](Entity entity, CollisionComponent& collision,
															  [[maybe_unused]] TransformComponent& transform) {
		if (!collision.enabled || !IsEntityActiveInHierarchy(world, entity)) {
			return;
		}
		if (!collision.shape.enabled) {
			return;
		}
		// 衝突判定と同じWorld形状を描く
		ResolvedWorldTransform resolved{};
		if (!TransformWorldUtility::ResolveWorldTransform(world, entity, resolved)) {
			return;
		}
		const CollisionShapeInstance instance = CollisionShapeUtility::BuildShapeInstance(entity, collision.shape, 0, resolved);
		DrawCollisionShape(instance, IsCollisionColliding(world, entity));
	});
#endif
}
