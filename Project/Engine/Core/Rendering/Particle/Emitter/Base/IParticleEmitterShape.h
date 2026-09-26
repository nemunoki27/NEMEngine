#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Structures/ParticleEmitterStructures.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	IParticleEmitterShape class
	//	発生形状ごとの処理のインターフェース、状態を持たず共有される
	//============================================================================
	class IParticleEmitterShape {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IParticleEmitterShape() = default;
		virtual ~IParticleEmitterShape() = default;

		// JSONから形状パラメータを読み込む
		virtual void FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const = 0;
		// JSONへ形状パラメータを書き出す
		virtual void ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const = 0;

		// 発生位置と方向を決める、ローカル空間で返す
		virtual void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D) const = 0;
		// 発生順を使う形状はこちらを実装する
		virtual void InitParticle(Vector3& position, Vector3& direction,
			const ParticleEmitterSettings& settings, bool is2D,
			[[maybe_unused]] const ParticleSpawnIndex& spawnIndex) const;

		//--------- accessor -----------------------------------------------------

		// 2D空間で使えるか
		virtual bool Supports2D() const { return false; }
		// 3D空間で使えるか
		virtual bool Supports3D() const { return true; }
	};
} // Engine
