#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <vector>

namespace Engine {

	enum class ParticleEventKind : uint8_t { Collision, Death, Time, PhaseTransition };

	// GPU上で次回更新の別Systemへ渡す設定
	struct ParticleEventRouteSettings {

		bool enabled = true;
		ParticleEventKind kind = ParticleEventKind::Collision;
		SceneEntityReference target{};
		uint32_t phase = UINT32_MAX;
		uint32_t count = 1;
		float time = 0.5f;
		float probability = 1.0f;
		bool inheritPosition = true;
		bool inheritNormal = true;
		bool inheritVelocity = false;
		bool inheritColor = true;
		bool inheritSeed = true;
	};

	struct ParticlePaintSettings {

		bool enabled = false;
		float radius = 0.1f;
		float hardness = 0.8f;
		Color4 color = Color4::White();
	};

	struct ParticleImpactDecalSettings {

		bool enabled = false;
		AssetID material{};
		Vector3 size{0.2f, 0.2f, 0.1f};
		Color4 color = Color4::White();
		float lifetime = 5.0f;
		float fadeDuration = 1.0f;
		uint32_t capacity = 1024;
		uint32_t receiverRenderingLayerMask = 0x00ffffffu;
	};

	struct ParticleFeedbackSettings {

		static constexpr uint32_t kMaxEventCapacity = 65536;
		static constexpr uint32_t kMaxRoutes = 32;
		static constexpr uint32_t kMaxDecalCapacity = 16384;

		uint32_t eventCapacity = 4096;
		std::vector<ParticleEventRouteSettings> events;
		ParticlePaintSettings paint{};
		ParticleImpactDecalSettings decal{};
	};

	// 編集値を上限と有限値の範囲へ収める
	void SanitizeParticleFeedbackSettings(ParticleFeedbackSettings& settings);
	// 保存値を既存の参照形式で読み書きする
	void from_json(const nlohmann::json& in, ParticleFeedbackSettings& settings);
	void to_json(nlohmann::json& out, const ParticleFeedbackSettings& settings);
}
