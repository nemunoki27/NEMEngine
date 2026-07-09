#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Emitter/Base/IParticleEmitterShape.h>

// c++
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticleEmitterShapeRegistry class
	//	発生形状から処理を引くレジストリ、各形状は自己登録する
	//============================================================================
	class ParticleEmitterShapeRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 形状を登録する、登録済みなら何もしない
		uint32_t Register(ParticleEmitterShape shape, std::unique_ptr<IParticleEmitterShape> instance);

		//--------- accessor -----------------------------------------------------

		// 形状から処理を取得する、未登録ならnullptr
		const IParticleEmitterShape* Find(ParticleEmitterShape shape) const;

		// 空間で使える形状一覧をenum順で取得する、エディターの選択候補に使う
		std::vector<ParticleEmitterShape> GetShapes(bool is2D) const;

		// 登録済みの全形状を取得する
		const std::unordered_map<ParticleEmitterShape, std::unique_ptr<IParticleEmitterShape>>& GetMap() const { return shapes_; }

		// シングルトン
		static ParticleEmitterShapeRegistry& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 形状から処理へのマップ
		std::unordered_map<ParticleEmitterShape, std::unique_ptr<IParticleEmitterShape>> shapes_;
	};

	//============================================================================
	//	ParticleEmitterShapeRegistry macros
	//============================================================================
#define ENGINE_REGISTER_PARTICLE_EMITTER_SHAPE(T, ShapeValue) \
    inline const uint32_t kParticleEmitterShape_##T = Engine::ParticleEmitterShapeRegistry::GetInstance().Register( \
        ShapeValue, std::make_unique<T>());
} // Engine
