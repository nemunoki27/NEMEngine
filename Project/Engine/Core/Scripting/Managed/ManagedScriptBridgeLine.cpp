#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineImmediateBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>

// c++
#include <vector>

namespace Engine {

	namespace {

		// ManagedLinePointをエンジンのLinePointへ変換する
		LinePoint ToLinePoint(const ManagedLinePoint& src) {

			LinePoint point{};
			point.position = Vector3(src.position.x, src.position.y, src.position.z);
			point.color = Color4(src.color.r, src.color.g, src.color.b, src.color.a);
			point.thickness = src.thickness;
			return point;
		}
	}
	void ManagedScriptRuntime::LineSetPointsCallback(ManagedNativeEntity entity,
		const ManagedLinePoint* points, int32_t count, int32_t loop) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world || count < 0 || (count > 0 && !points)) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		LineRendererComponent* line = world->IsAlive(resolved) ?
			world->TryGetComponentForBinding<LineRendererComponent>(resolved) : nullptr;
		if (!line) {
			return;
		}

		// Managed配列を一度変換し、DynamicBufferをまとめて差し替える
		std::vector<LinePoint> converted{};
		if (points != nullptr && count > 0) {

			converted.reserve(static_cast<size_t>(count));
			for (int32_t i = 0; i < count; ++i) {
				converted.emplace_back(ToLinePoint(points[i]));
			}
		}
		// Bufferの追加も予約し、Componentのアドレスを移動させない
		const int32_t typeID = static_cast<int32_t>(ComponentTypeRegistry::GetInstance().GetID<LinePoint>());
		AddComponentCallback(entity, typeID);
		if (!DynamicBufferMutateCallback(entity, typeID, sizeof(LinePoint),
			static_cast<int32_t>(ManagedDynamicBufferOperation::Replace), 0, converted.data(), count)) {
			return;
		}
		line->loop = (loop != 0);
		world->MarkComponentModified<LineRendererComponent>(resolved);
	}

	int32_t ManagedScriptRuntime::LineAddPointCallback(ManagedNativeEntity entity, ManagedLinePoint point) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return -1;
		}
		const Entity resolved = ResolveEntity(entity);
		LineRendererComponent* line = world->IsAlive(resolved) ?
			world->TryGetComponentForBinding<LineRendererComponent>(resolved) : nullptr;
		if (!line) {
			return -1;
		}
		const int32_t typeID = static_cast<int32_t>(ComponentTypeRegistry::GetInstance().GetID<LinePoint>());
		AddComponentCallback(entity, typeID);
		const LinePoint converted = ToLinePoint(point);
		if (!DynamicBufferMutateCallback(entity, typeID, sizeof(LinePoint),
			static_cast<int32_t>(ManagedDynamicBufferOperation::Append), 0, &converted, 1)) {
			return -1;
		}
		// 追加した点の位置をC#へ返す、UpdatePointの対象指定に使う
		return DynamicBufferLengthCallback(entity, typeID, sizeof(LinePoint)) - 1;
	}

	void ManagedScriptRuntime::LineUpdatePointCallback(ManagedNativeEntity entity, ManagedLinePoint point) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return;
		}
		const Entity resolved = ResolveEntity(entity);
		LineRendererComponent* line = world->IsAlive(resolved) ?
			world->TryGetComponentForBinding<LineRendererComponent>(resolved) : nullptr;
		if (!line) {
			return;
		}
		// ClearやSetPoints後に残った古いindexを弾く
		const int32_t typeID = static_cast<int32_t>(ComponentTypeRegistry::GetInstance().GetID<LinePoint>());
		const LinePoint converted = ToLinePoint(point);
		DynamicBufferMutateCallback(entity, typeID, sizeof(LinePoint),
			static_cast<int32_t>(ManagedDynamicBufferOperation::SetElement), point.index, &converted, 1);
	}

	void ManagedScriptRuntime::LineDrawImmediateCallback(const ManagedLinePoint* points,
		int32_t count, int32_t loop, int32_t is2D, ManagedAssetGUID materialID) {

		if (points == nullptr || count < 2) {
			return;
		}

		// 即時バッファへ積むため一旦エンジン型へ変換する
		std::vector<LinePoint> converted;
		converted.reserve(static_cast<size_t>(count));
		for (int32_t i = 0; i < count; ++i) {
			converted.emplace_back(ToLinePoint(points[i]));
		}
		LineImmediateBuffer::GetInstance().AddPolyline(converted.data(), static_cast<uint32_t>(count),
			true, loop != 0, is2D != 0, ToAssetID(materialID));
	}

	void ManagedScriptRuntime::LineDrawSphereImmediateCallback(ManagedVector3 center, float radius,
		ManagedColor4 color, int32_t division, float thickness, ManagedAssetGUID materialID) {

		const uint32_t safeDivision = division < 3 ? 3u : static_cast<uint32_t>(division);
		LineImmediateBuffer::GetInstance().AddSphere(
			Vector3(center.x, center.y, center.z),
			radius, Color4(color.r, color.g, color.b, color.a),
			safeDivision, thickness, ToAssetID(materialID));
	}

	void ManagedScriptRuntime::LineDrawShapeCallback(const ManagedLineShape* shape) {

		if (shape == nullptr) {
			return;
		}

		const Color4 color(shape->color.r, shape->color.g, shape->color.b, shape->color.a);
		const Vector3 a(shape->a.x, shape->a.y, shape->a.z);
		const Vector3 b(shape->b.x, shape->b.y, shape->b.z);
		const Quaternion rotation(shape->rotation.x, shape->rotation.y, shape->rotation.z, shape->rotation.w);
		const uint32_t division = shape->division < 3 ? 3u : static_cast<uint32_t>(shape->division);

		// 形状種別ごとに線分リストへ展開する
		std::vector<LinePoint> segments;
		switch (static_cast<ManagedLineShapeKind>(shape->shapeType)) {
		case ManagedLineShapeKind::Circle2D:
			LineShapeBuilder::BuildCircle2D(Vector2(a.x, a.y), shape->radius, color, division, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Rect2D:
			LineShapeBuilder::BuildRect2D(Vector2(a.x, a.y), Vector2(b.x, b.y), rotation, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Hemisphere:
			LineShapeBuilder::BuildHemisphere(a, shape->radius, rotation, color, division, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::AABB:
			LineShapeBuilder::BuildAABB(a, b, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::OBB:
			LineShapeBuilder::BuildOBB(a, b, rotation, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Cone:
			LineShapeBuilder::BuildCone(a, shape->radius, shape->radius2, shape->height, rotation, color, division, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Arrow:
			LineShapeBuilder::BuildArrow(a, shape->height, rotation, color, shape->thickness, segments);
			break;
		case ManagedLineShapeKind::Axis:
			LineShapeBuilder::BuildAxis(a, rotation, shape->height, shape->thickness, segments);
			break;
		default:
			return;
		}

		if (segments.size() < 2) {
			return;
		}
		// 形状は2点ずつ独立した線分リストなのでconnected=falseで積む
		LineImmediateBuffer::GetInstance().AddPolyline(segments.data(), static_cast<uint32_t>(segments.size()),
			false, false, shape->is2D != 0, ToAssetID(shape->materialID));
	}
}
