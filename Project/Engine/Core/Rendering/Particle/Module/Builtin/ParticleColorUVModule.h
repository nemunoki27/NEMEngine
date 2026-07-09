#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

namespace Engine {

	//============================================================================
	//	ParticleColorUVModule class
	//	カラーテクスチャのUVを補間かスクロールで動かす
	//============================================================================
	// UVの更新方法
	enum class ParticleUVUpdateType :
		uint8_t {

		Lerp,   // 開始と終了を進行度で補間する
		Scroll, // 一定速度で流し続ける
	};

	class ParticleColorUVModule :
		public IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleColorUVModule() = default;
		~ParticleColorUVModule() override = default;

		void FromJson(const nlohmann::json& params) override;
		nlohmann::json ToJson() const override;
		bool DrawImGui() override;

		void OnUpdate(std::span<Particle> alive, float deltaTime) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// UVの更新方法
		ParticleUVUpdateType updateType_ = ParticleUVUpdateType::Lerp;

		// UVオフセットの始点と終点
		Vector2 startOffset_ = Vector2::AnyInit(0.0f);
		Vector2 endOffset_ = Vector2::AnyInit(0.0f);
		// UVスケールの始点と終点
		Vector2 startScale_ = Vector2::AnyInit(1.0f);
		Vector2 endScale_ = Vector2::AnyInit(1.0f);
		// スクロール速度、1秒あたりのUV移動量
		Vector2 scrollSpeed_ = Vector2::AnyInit(0.0f);
		// イージング
		EasingType easingType_ = EasingType::EaseOutSine;
	};

	ENGINE_REGISTER_PARTICLE_MODULE(ParticleColorUVModule, "ColorUV");
} // Engine
