#include "SceneComponentOverlayCollector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_map>

//============================================================================
//	local
//============================================================================
namespace {

	// 表示位置とカメラからの距離
	struct ProjectionResult {

		// SceneView内へ射影できたかどうか
		bool visible = false;
		// SceneView左上基準のピクセル座標
		Engine::Vector2 screen = Engine::Vector2::AnyInit(0.0f);
		// カメラView空間での奥行きで近いOverlayの優先順位に使う
		float viewDepth = 0.0f;
		// SceneViewカメラとの距離でアイコンサイズと非表示判定に使う
		float distance = 0.0f;
	};

	// 編集中の現在値からワールド行列を求める
	Engine::Matrix4x4 ComputeWorldMatrixFromTransform(const Engine::ECSWorld& world, Engine::Entity entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::TransformComponent>(entity)) {
			return Engine::Matrix4x4::Identity();
		}

		const Engine::TransformComponent& transform = world.GetComponent<Engine::TransformComponent>(entity);
		Engine::Matrix4x4 worldMatrix = Engine::MakeLocalMatrix(transform);
		if (world.HasComponent<Engine::HierarchyComponent>(entity)) {
			const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
			if (world.IsAlive(hierarchy.parent)) {
				worldMatrix = worldMatrix * ComputeWorldMatrixFromTransform(world, hierarchy.parent);
			}
		}
		return worldMatrix;
	}

	// 編集中の現在値から表示位置を求める
	Engine::Vector3 GetWorldPosition(const Engine::ECSWorld& world, Engine::Entity entity) {

		return ComputeWorldMatrixFromTransform(world, entity).GetTranslationValue();
	}

	// ワールド座標をSceneViewカメラでピクセル座標へ変換し、範囲外なら非表示にする
	ProjectionResult ProjectToSceneView(const Engine::ResolvedCameraView& camera,
		uint32_t width, uint32_t height, const Engine::Vector3& worldPosition) {

		ProjectionResult result{};
		if (!camera.valid || width == 0 || height == 0) {
			return result;
		}

		// 背面、Near/Far外のOverlayは描画もピックもしない
		const Engine::Vector3 viewPosition = Engine::Vector3::Transform(worldPosition, camera.matrices.viewMatrix);
		if (viewPosition.z < camera.nearClip || camera.farClip < viewPosition.z) {
			return result;
		}

		// NDC範囲外はSceneView外なので候補から外す
		const Engine::Vector3 ndc = Engine::Vector3::Transform(worldPosition, camera.matrices.viewProjectionMatrix);
		if (ndc.z < 0.0f || 1.0f < ndc.z ||
			ndc.x < -1.0f || 1.0f < ndc.x ||
			ndc.y < -1.0f || 1.0f < ndc.y) {
			return result;
		}

		result.visible = true;
		result.screen.x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
		result.screen.y = (0.5f - ndc.y * 0.5f) * static_cast<float>(height);
		result.viewDepth = viewPosition.z;
		result.distance = Engine::Vector3::Length(worldPosition - camera.cameraPos);
		return result;
	}

	// 近距離は大きく、遠距離は小さくなるようライトアイコンのピクセルサイズを決める
	float ComputeLightIconPixelSize(float distance, const Engine::SceneComponentOverlaySettings& settings) {

		const float denominator = (std::max)(settings.lightIconFarDistance - settings.lightIconNearDistance, 0.001f);
		const float t = Math::Saturate((distance - settings.lightIconNearDistance) / denominator);
		return Math::Lerp(settings.lightIconMaxPixelSize, settings.lightIconMinPixelSize, t);
	}

	// 完全に重なるアイコンをフレームごとに揺れない順序で少しずらす
	Engine::Vector2 ComputeDeterministicOffset(uint32_t ordinal, const Engine::SceneComponentOverlaySettings& settings) {

		static const Engine::Vector2 kDirections[] = {
			Engine::Vector2(0.0f, 0.0f),
			Engine::Vector2(1.0f, 0.0f),
			Engine::Vector2(-1.0f, 0.0f),
			Engine::Vector2(0.0f, 1.0f),
			Engine::Vector2(0.0f, -1.0f),
			Engine::Vector2(1.0f, 1.0f),
			Engine::Vector2(-1.0f, 1.0f),
			Engine::Vector2(1.0f, -1.0f),
			Engine::Vector2(-1.0f, -1.0f),
		};
		const Engine::Vector2 direction = kDirections[ordinal % std::size(kDirections)];
		return direction * settings.overlapPixelOffset;
	}

	// 重なり順を割り当て、描画と選択の矩形を揃える
	Engine::SceneComponentOverlayItem MakeOverlayItem(Engine::Entity entity,
		const Engine::Vector3& position, ProjectionResult projection,
		const Engine::SceneComponentOverlayRegistry::Registration& registration,
		const Engine::SceneComponentOverlaySettings& settings, float size,
		std::unordered_map<uint64_t, uint32_t>& overlapCounts) {

		const uint64_t overlapKey = (static_cast<uint64_t>(std::lround(projection.screen.x)) << 32) |
			static_cast<uint32_t>(std::lround(projection.screen.y));
		const uint32_t overlapOrdinal = overlapCounts[overlapKey]++;
		projection.screen += ComputeDeterministicOffset(overlapOrdinal, settings);
		const Engine::Vector2 halfSize(size * 0.5f, size * 0.5f);

		Engine::SceneComponentOverlayItem item{};
		item.kind = registration.overlayKind;
		item.componentKind = registration.componentKind;
		item.entity = entity;
		item.asset = registration.asset;
		item.worldPosition = position;
		item.screenCenter = projection.screen;
		item.rectMin = projection.screen - halfSize;
		item.rectMax = projection.screen + halfSize;
		item.viewDepth = projection.viewDepth;
		item.distanceToCamera = projection.distance;
		return item;
	}

	// ライトの色と有効状態をアイコンへ反映
	void AddLightItem(const Engine::ECSWorld& world, Engine::Entity entity,
		Engine::SceneComponentOverlayComponentKind kind, const Engine::Color4& lightColor, bool enabled,
		float spriteRotationRadians,
		const Engine::SceneComponentOverlayRegistry& registry,
		const Engine::ResolvedCameraView& camera, const Engine::ResolvedRenderView& view,
		const Engine::SceneComponentOverlaySettings& settings,
		uint32_t& stableOrder, std::unordered_map<uint64_t, uint32_t>& overlapCounts,
		Engine::SceneComponentOverlayItemList& outItems) {

		// 非アクティブなEntityを表示から除外
		if (!Engine::IsEntityActiveInHierarchy(world, entity)) {
			return;
		}
		const auto* registration = registry.Find(kind);
		if (!registration) {
			return;
		}

		// ライトアイコンはEntityの回転・スケールを使わず、位置だけをSceneViewへ射影する
		const Engine::Vector3 position = GetWorldPosition(world, entity);
		ProjectionResult projection = ProjectToSceneView(camera, view.width, view.height, position);
		if (!projection.visible || settings.lightIconHideDistance < projection.distance) {
			return;
		}

		// 非表示判定前に重なり順を確定
		const float size = ComputeLightIconPixelSize(projection.distance, settings);
		Engine::SceneComponentOverlayItem item = MakeOverlayItem(
			entity, position, projection, *registration, settings, size, overlapCounts);
		item.kind = Engine::SceneComponentOverlayKind::LightIcon;
		item.componentKind = kind;
		if (size < settings.lightIconCullPixelSize) {
			return;
		}

		item.spriteRotationRadians = spriteRotationRadians;
		item.color = Engine::Color4(lightColor.r, lightColor.g, lightColor.b,
			lightColor.a * (enabled ? 1.0f : settings.disabledAlpha));
		item.enabled = enabled;
		item.stableOrder = stableOrder++;
		outItems.emplace_back(item);
	}

	// カメラの有効状態をアイコンへ反映
	void AddCameraItem(const Engine::ECSWorld& world, Engine::Entity entity,
		const Engine::PerspectiveCameraComponent& cameraComponent,
		const Engine::SceneComponentOverlayRegistry& registry,
		const Engine::ResolvedCameraView& camera, const Engine::ResolvedRenderView& view,
		const Engine::SceneComponentOverlaySettings& settings,
		uint32_t& stableOrder, std::unordered_map<uint64_t, uint32_t>& overlapCounts,
		Engine::SceneComponentOverlayItemList& outItems) {

		// シーン上で無効なEntityは表示しないが、コンポーネントenabled=falseは半透明表示する
		if (!Engine::IsEntityActiveInHierarchy(world, entity)) {
			return;
		}
		const auto* registration = registry.Find(Engine::SceneComponentOverlayComponentKind::PerspectiveCamera);
		if (!registration) {
			return;
		}

		const Engine::Vector3 position = GetWorldPosition(world, entity);
		ProjectionResult projection = ProjectToSceneView(camera, view.width, view.height, position);
		if (!projection.visible || settings.lightIconHideDistance < projection.distance) {
			return;
		}

		// カメラは距離による最小サイズでも表示
		const float size = ComputeLightIconPixelSize(projection.distance, settings);
		Engine::SceneComponentOverlayItem item = MakeOverlayItem(
			entity, position, projection, *registration, settings, size, overlapCounts);
		item.kind = Engine::SceneComponentOverlayKind::CameraIcon;
		item.componentKind = Engine::SceneComponentOverlayComponentKind::PerspectiveCamera;
		item.color = Engine::Color4(1.0f, 1.0f, 1.0f, cameraComponent.common.enabled ? 1.0f : settings.disabledAlpha);
		item.enabled = cameraComponent.common.enabled;
		item.stableOrder = stableOrder++;
		outItems.emplace_back(item);
	}
}

//============================================================================
//	SceneComponentOverlayCollector classMethods
//============================================================================
void Engine::SceneComponentOverlayCollector::Collect(const ECSWorld& world, const ResolvedRenderView& view,
	const SceneComponentOverlayRegistry& registry, const SceneComponentOverlaySettings& settings,
	SceneComponentOverlayItemList& outItems) const {

	outItems.clear();
	if (!view.valid) {
		return;
	}
	const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective);
	if (!camera || !camera->valid) {
		return;
	}

	uint32_t stableOrder = 0;
	std::unordered_map<uint64_t, uint32_t> overlapCounts{};

	// 同一Entityに複数コンポーネントがある場合も、コンポーネントごとに表示アイテムを作る
	world.ForEach<DirectionalLightComponent>([&](Entity entity, const DirectionalLightComponent& light) {
		AddLightItem(world, entity, SceneComponentOverlayComponentKind::DirectionalLight, light.color, light.enabled,
			0.0f, registry, *camera, view, settings, stableOrder, overlapCounts, outItems);
	});
	world.ForEach<PointLightComponent>([&](Entity entity, const PointLightComponent& light) {
		AddLightItem(world, entity, SceneComponentOverlayComponentKind::PointLight, light.color, light.enabled,
			0.0f, registry, *camera, view, settings, stableOrder, overlapCounts, outItems);
	});
	world.ForEach<RectLightComponent>([&](Entity entity, const RectLightComponent& light) {
		AddLightItem(world, entity, SceneComponentOverlayComponentKind::RectLight, light.color, light.enabled,
			0.0f, registry, *camera, view, settings, stableOrder, overlapCounts, outItems);
		});
	world.ForEach<SpotLightComponent>([&](Entity entity, const SpotLightComponent& light) {
		AddLightItem(world, entity, SceneComponentOverlayComponentKind::SpotLight, light.color, light.enabled,
			0.0f, registry, *camera, view, settings, stableOrder, overlapCounts, outItems);
	});
	world.ForEach<PerspectiveCameraComponent>([&](Entity entity, const PerspectiveCameraComponent& cameraComponent) {
		AddCameraItem(world, entity, cameraComponent, registry, *camera, view, settings,
			stableOrder, overlapCounts, outItems);
		});

	// 近いものを優先し、同じ奥行きなら登録順で安定させる
	std::sort(outItems.begin(), outItems.end(), [](const SceneComponentOverlayItem& lhs,
		const SceneComponentOverlayItem& rhs) {
			if (lhs.viewDepth != rhs.viewDepth) {
				return lhs.viewDepth < rhs.viewDepth;
			}
			return lhs.stableOrder < rhs.stableOrder;
		});
}
