#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Parametric/IParticleParametricShape.h>

// c++
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticleParametricShapeRegistry class
	//	Builtinパラメトリック形状を明示登録し、描画形状から処理を引く
	//============================================================================
	class ParticleParametricShapeRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 形状を登録する、登録済みなら何もしない
		uint32_t Register(PrimitiveType type, std::unique_ptr<IParticleParametricShape> instance);

		//--------- accessor -----------------------------------------------------

		// 形状から処理を取得する、未登録ならnullptr
		const IParticleParametricShape* Find(PrimitiveType type) const;

		// 登録済みの形状一覧をenum順で取得する、エディターの選択候補に使う
		std::vector<PrimitiveType> GetTypes() const;

		// シングルトン
		static ParticleParametricShapeRegistry& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		ParticleParametricShapeRegistry();

		//--------- variables ----------------------------------------------------

		// 形状から処理へのマップ
		std::unordered_map<PrimitiveType, std::unique_ptr<IParticleParametricShape>> shapes_;
	};
} // Engine
