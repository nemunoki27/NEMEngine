#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleTypes.h>

// c++
#include <span>

// json
#include <json.hpp>

namespace Engine {

	// モジュールの粒子処理方式
	enum class ParticleModuleExecutionMode :
		uint8_t {

		None,
		PerParticle,
		Batch,
	};

	//============================================================================
	//	IParticleModule class
	//	粒子の発生と更新を担当するモジュールのインターフェース、状態を持たず共有される
	//============================================================================
	class IParticleModule {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IParticleModule() = default;
		virtual ~IParticleModule() = default;

		// JSONパラメータの適用
		virtual void FromJson(const nlohmann::json& params) = 0;
		// JSONパラメータの書き出し
		virtual nlohmann::json ToJson() const = 0;

		// このフレームの発生数を返す、蓄積値はエミッターごとにコンポーネント側が持つ
		virtual uint32_t OnEmit([[maybe_unused]] float deltaTime, [[maybe_unused]] float& emitAccumulator) { return 0; }
		// 新規発生した粒子の処理方式
		virtual ParticleModuleExecutionMode GetSpawnExecutionMode() const { return ParticleModuleExecutionMode::None; }
		// 生存粒子の処理方式
		virtual ParticleModuleExecutionMode GetUpdateExecutionMode() const { return ParticleModuleExecutionMode::None; }
		// 新規発生した粒子を初期化する
		virtual void OnSpawn([[maybe_unused]] Particle& particle) {}
		// 生存粒子を更新する
		virtual void OnUpdate([[maybe_unused]] Particle& particle, [[maybe_unused]] float deltaTime) {}
		// 新規発生した粒子を一括初期化する
		virtual void OnSpawnBatch([[maybe_unused]] std::span<Particle> particles) {}
		// 生存粒子を一括更新する
		virtual void OnUpdateBatch([[maybe_unused]] std::span<Particle> particles, [[maybe_unused]] float deltaTime) {}
	};
} // Engine
