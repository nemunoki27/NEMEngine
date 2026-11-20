#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Curves/AnimationCurve.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Assets/RendererRenderSettings.h>
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/Time/LocalTime.h>

#include <deque>

namespace Engine {

	enum class TrailMode : uint8_t { SinglePoint, TwoPoints };
	enum class TrailAlignment : uint8_t { View, TransformZ };
	enum class TrailTextureMode : uint8_t { Stretch, Tile };

	// シーンとプレファブに保存する描画・サンプリング設定
	struct TrailRendererSettings {
		TrailRendererSettings();
		TrailMode mode = TrailMode::SinglePoint;
		TrailAlignment alignment = TrailAlignment::View;
		TrailTextureMode textureMode = TrailTextureMode::Stretch;
		bool is2D = false, emitting = true;
		float lifetime = 0.5f, minVertexDistance = 0.05f, width = 0.1f, widthMultiplier = 1.0f;
		SceneEntityReference root{}, tip{};
		CurveFloat widthCurve;
		CurveColor4 colorGradient;
		TimeSourceSettings timeSource{};
		AssetID material{};
		MaterialInstanceParameters materialInstance{};
		int32_t layer = 0, order = 0;
		bool visible = true;
		RendererRenderSettings renderSettings{.cullMode = RendererCullMode::NONE, .depthWrite = false};
		uint32_t renderingLayerMask = 1u;
		BlendMode blendMode = BlendMode::Normal;
		RenderPhase queue = RenderPhase::Transparent;
	};

	struct TrailSample {
		Vector3 root{}, tip{}, normal{0, 0, 1};
		double time = 0.0;
		bool breakBefore = false;
	};

	// 生成中の帯とTimelineによるプレビュー状態
	struct TrailRendererRuntime {
		std::deque<TrailSample> points;
		double time = 0.0;
		bool breakBefore = true, timelineHoldsHistory = false;
		Entity rootOverride = Entity::Null(), tipOverride = Entity::Null();
	};
	struct TrailRendererRecord {
		TrailRendererSettings settings;
		TrailRendererRuntime runtime;
		UUID owner{};
	};
	struct TrailRendererStorageTag;
	using TrailRendererStorage = GenerationalPool<TrailRendererRecord, TrailRendererStorageTag>;

	struct TrailRendererComponent {
		static constexpr bool kHasECSHooks = true;
		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Render;
		static constexpr ComponentChangeChannel kTransformChannels = ComponentChangeChannel::Render;
		TrailRendererStorage::Handle handle{};
		static void OnAdded(ECSWorld&, const Entity&, TrailRendererComponent&);
		static void OnRemoved(ECSWorld&, const Entity&);
		static void InitializeStorage(ECSWorld&, const Entity&, TrailRendererComponent&);
		static void ReleaseStorage(ECSWorld&, const Entity&, TrailRendererComponent&);
		static void CloneStorage(const ECSWorld&, ECSWorld&, const Entity&, const TrailRendererComponent&, TrailRendererComponent&);
		static void DeserializeECS(ECSWorld&, const Entity&, const nlohmann::json&, TrailRendererComponent&);
		static void SerializeECS(const ECSWorld&, const Entity&, const TrailRendererComponent&, nlohmann::json&);
	};

	void from_json(const nlohmann::json& in, TrailRendererSettings& settings);
	void to_json(nlohmann::json& out, const TrailRendererSettings& settings);
	TrailRendererRecord* TryGetTrailRenderer(ECSWorld& world, Entity owner);
	const TrailRendererRecord* TryGetTrailRenderer(const ECSWorld& world, Entity owner);
	void SetTrailRendererSettings(ECSWorld& world, Entity owner, const TrailRendererSettings& settings);
	void AgeTrail(TrailRendererRecord& record, double deltaTime);
	void AppendTrailSample(TrailRendererRecord& record, const Vector3& root, const Vector3& tip, const Vector3& normal, bool force = false);
	void ClearTrail(ECSWorld& world, Entity owner);
	void BreakTrail(ECSWorld& world, Entity owner);
	void SetTrailEmitting(ECSWorld& world, Entity owner, bool emitting);
	void AddTrailPosition(ECSWorld& world, Entity owner, const Vector3& position);
	void AddTrailSegment(ECSWorld& world, Entity owner, const Vector3& root, const Vector3& tip);
}
