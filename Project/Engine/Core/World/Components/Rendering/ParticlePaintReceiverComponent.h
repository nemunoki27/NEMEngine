#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Core/ComponentType.h>
#include <Engine/Core/Foundation/Math/Math.h>

namespace Engine {

	class ECSWorld;
	struct Entity;

	enum class ParticlePaintProjection : uint8_t { UV, PlanarXZ };

	// 実行時の塗装Textureを受け取る面の設定
	struct ParticlePaintReceiverComponent {

		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::None;
		static constexpr ComponentChangeChannel kTransformChannels = ComponentChangeChannel::None;

		bool enabled = true;
		ParticlePaintProjection projection = ParticlePaintProjection::PlanarXZ;
		uint32_t resolution = 512;
		Vector2 planarSize{10.0f, 10.0f};
		float opacity = 1.0f;
		uint32_t clearSerial = 0;
	};
	static_assert(sizeof(ParticlePaintReceiverComponent) <= 32);

	// 次回描画前に実行時の塗装だけを消す
	void ClearParticlePaint(ECSWorld& world, const Entity& entity);
	// 保存値へ実行時の消去要求を含めない
	void from_json(const nlohmann::json& in, ParticlePaintReceiverComponent& component);
	void to_json(nlohmann::json& out, const ParticlePaintReceiverComponent& component);
}
