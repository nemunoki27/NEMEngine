#include "SceneComponentOverlayCollector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>

// c++
#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_map>

//============================================================================
//	local
//============================================================================
namespace {

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

	// 0..1へ丸める小さな補助関数
	float Clamp01(float value) {

		return std::clamp(value, 0.0f, 1.0f);
	}

	// アイコンサイズ補間用の線形補間
	float Lerp(float a, float b, float t) {

		return a + (b - a) * t;
	}

	// Transformの現在値からワールド行列を作りworldMatrixキャッシュが未更新でもSceneView位置を合わせるために使う
	Engine::Matrix4x4 ComputeWorldMatrixFromTransform(Engine::ECSWorld& world, const Engine::Entity& entity) {

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

	// Overlayの基準位置はTransformComponentの現在位置だけを使う
	Engine::Vector3 GetWorldPosition(Engine::ECSWorld& world, const Engine::Entity& entity) {

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
		const float t = Clamp01((distance - settings.lightIconNearDistance) / denominator);
		return Lerp(settings.lightIconMaxPixelSize, settings.lightIconMinPixelSize, t);
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

	// ライトコンポーネント1つから、2Dアイコン描画用のOverlayItemを作る
	void AddLightItem(Engine::ECSWorld& world, const Engine::Entity& entity,
		Engine::SceneComponentOverlayComponentKind kind, const Engine::Color4& lightColor, bool enabled,
		float spriteRotationRadians,
		const Engine::SceneComponentOverlayRegistry& registry,
		const Engine::ResolvedCameraView& camera, const Engine::ResolvedRenderView& view,
		const Engine::SceneComponentOverlaySettings& settings,
		uint32_t& stableOrder, std::unordered_map<uint64_t, uint32_t>& overlapCounts,
		Engine::SceneComponentOverlayItemList& outItems) {

		// 非activeなEntityはエディターOverlayにも表示しない
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

		// 同一ピクセルへ複数出た場合も決定的にオフセットする
		const uint64_t overlapKey =
			(static_cast<uint64_t>(std::lround(projection.screen.x)) << 32) |
			static_cast<uint32_t>(std::lround(projection.screen.y));
		uint32_t overlapOrdinal = overlapCounts[overlapKey]++;
		const Engine::Vector2 offset = ComputeDeterministicOffset(overlapOrdinal, settings);
		projection.screen += offset;

		const float size = ComputeLightIconPixelSize(projection.distance, settings);
		const Engine::Vector2 halfSize(size * 0.5f, size * 0.5f);

		// PickerはこのrectMin/rectMaxと同じ矩形でCPU判定する
		Engine::SceneComponentOverlayItem item{};
		item.kind = Engine::SceneComponentOverlayKind::LightIcon;
		item.componentKind = kind;
		item.entity = entity;
		item.asset = registration->asset;
		item.worldPosition = position;
		item.screenCenter = projection.screen;
		item.rectMin = projection.screen - halfSize;
		item.rectMax = projection.screen + halfSize;
		item.spriteRotationRadians = spriteRotationRadians;
		item.viewDepth = projection.viewDepth;
		item.distanceToCamera = projection.distance;
		item.color = Engine::Color4(lightColor.r, lightColor.g, lightColor.b, lightColor.a * (enabled ? 1.0f : settings.disabledAlpha));
		item.enabled = enabled;
		item.stableOrder = stableOrder++;
		outItems.emplace_back(item);
	}

	// PerspectiveCameraComponentから、2Dカメラアイコン描画用のOverlayItemを作る
	void AddCameraItem(Engine::ECSWorld& world, const Engine::Entity& entity,
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

		// ライトアイコンと同じく、同一点のOverlayは決定的に少しずらす
		const uint64_t overlapKey =
			(static_cast<uint64_t>(std::lround(projection.screen.x)) << 32) |
			static_cast<uint32_t>(std::lround(projection.screen.y));
		uint32_t overlapOrdinal = overlapCounts[overlapKey]++;
		const Engine::Vector2 offset = ComputeDeterministicOffset(overlapOrdinal, settings);
		projection.screen += offset;

		const float size = ComputeLightIconPixelSize(projection.distance, settings);
		const Engine::Vector2 halfSize(size * 0.5f, size * 0.5f);

		// Pickerはカメラも描画矩形と同じCPU矩形で判定する
		Engine::SceneComponentOverlayItem item{};
		item.kind = Engine::SceneComponentOverlayKind::CameraIcon;
		item.componentKind = Engine::SceneComponentOverlayComponentKind::PerspectiveCamera;
		item.entity = entity;
		item.asset = registration->asset;
		item.worldPosition = position;
		item.screenCenter = projection.screen;
		item.rectMin = projection.screen - halfSize;
		item.rectMax = projection.screen + halfSize;
		item.viewDepth = projection.viewDepth;
		item.distanceToCamera = projection.distance;
		item.color = Engine::Color4(1.0f, 1.0f, 1.0f, cameraComponent.common.enabled ? 1.0f : settings.disabledAlpha);
		item.enabled = cameraComponent.common.enabled;
		item.stableOrder = stableOrder++;
		outItems.emplace_back(item);
	}
}

//============================================================================
//	SceneComponentOverlayCollector classMethods
//============================================================================
void Engine::SceneComponentOverlayCollector::Collect(ECSWorld& world, const ResolvedRenderView& view,
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
	world.ForEach<DirectionalLightComponent>([&](const Entity& entity, DirectionalLightComponent& light) {
		AddLightItem(world, entity, SceneComponentOverlayComponentKind::DirectionalLight, light.color, light.enabled,
			0.0f, registry, *camera, view, settings, stableOrder, overlapCounts, outItems);
	});
	world.ForEach<PointLightComponent>([&](const Entity& entity, PointLightComponent& light) {
		AddLightItem(world, entity, SceneComponentOverlayComponentKind::PointLight, light.color, light.enabled,
			0.0f, registry, *camera, view, settings, stableOrder, overlapCounts, outItems);
	});
	world.ForEach<SpotLightComponent>([&](const Entity& entity, SpotLightComponent& light) {
		AddLightItem(world, entity, SceneComponentOverlayComponentKind::SpotLight, light.color, light.enabled,
			0.0f, registry, *camera, view, settings, stableOrder, overlapCounts, outItems);
	});
	world.ForEach<PerspectiveCameraComponent>([&](const Entity& entity, PerspectiveCameraComponent& cameraComponent) {
		AddCameraItem(world, entity, cameraComponent, registry, *camera, view, settings, stableOrder, overlapCounts, outItems);
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
