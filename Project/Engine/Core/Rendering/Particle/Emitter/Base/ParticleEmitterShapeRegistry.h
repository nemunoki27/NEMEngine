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
	//	Builtin発生形状を明示登録し、形状enumから処理を引く
	//============================================================================
	class ParticleEmitterShapeRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 形状を登録する、登録済みなら何もしない
		uint32_t Register(ParticleEmitterShape shape, std::unique_ptr<IParticleEmitterShape> instance);

		using DebugDrawFunction = void (*)(const ParticleEmitterSettings&, const Vector3&, const Quaternion&, bool);
		// 登録された補助描画へ発生形状を渡す
		void DrawDebugShape(const ParticleEmitterSettings& settings,
			const Vector3& center, const Quaternion& rotation, bool is2D) const;

		//--------- accessor -----------------------------------------------------

		// Editorの補助描画を接続する
		void SetDebugDrawFunction(DebugDrawFunction function) { debugDraw_ = function; }

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

		ParticleEmitterShapeRegistry();

		//--------- variables ----------------------------------------------------

		// 形状から処理へのマップ
		std::unordered_map<ParticleEmitterShape, std::unique_ptr<IParticleEmitterShape>> shapes_;
		// Editorが有効な間だけ呼ぶ補助描画
		DebugDrawFunction debugDraw_ = nullptr;
	};
} // Engine
