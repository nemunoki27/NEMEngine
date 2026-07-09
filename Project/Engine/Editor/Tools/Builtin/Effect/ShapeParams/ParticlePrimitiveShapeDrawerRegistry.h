#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/IParticlePrimitiveShapeDrawer.h>

// c++
#include <memory>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ParticlePrimitiveShapeDrawerRegistry class
	//	描画形状からパラメータ編集UIを引くレジストリ、各形状は自己登録する
	//============================================================================
	class ParticlePrimitiveShapeDrawerRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 形状を登録する、登録済みなら何もしない
		uint32_t Register(PrimitiveType type, std::unique_ptr<IParticlePrimitiveShapeDrawer> instance);

		//--------- accessor -----------------------------------------------------

		// 形状から編集UIを取得する、未登録ならnullptr
		const IParticlePrimitiveShapeDrawer* Find(PrimitiveType type) const;

		// シングルトン
		static ParticlePrimitiveShapeDrawerRegistry& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 形状から編集UIへのマップ
		std::unordered_map<PrimitiveType, std::unique_ptr<IParticlePrimitiveShapeDrawer>> drawers_;
	};

	//============================================================================
	//	ParticlePrimitiveShapeDrawerRegistry macros
	//============================================================================
#define ENGINE_REGISTER_PARTICLE_PRIMITIVE_SHAPE_DRAWER(T, TypeValue) \
    inline const uint32_t kParticlePrimitiveShapeDrawer_##T = Engine::ParticlePrimitiveShapeDrawerRegistry::GetInstance().Register( \
        TypeValue, std::make_unique<T>());
} // Engine
