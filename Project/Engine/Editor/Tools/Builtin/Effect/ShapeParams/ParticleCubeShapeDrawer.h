#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticlePrimitiveShapeDrawerRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleCubeShapeDrawer class
	//	Cube形状のパラメータ編集UI
	//============================================================================
	class ParticleCubeShapeDrawer :
		public IParticlePrimitiveShapeDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleCubeShapeDrawer() = default;
		~ParticleCubeShapeDrawer() override = default;

		bool DrawImGui(ParticleEffectAsset& asset) const override;
	};

	ENGINE_REGISTER_PARTICLE_PRIMITIVE_SHAPE_DRAWER(ParticleCubeShapeDrawer, PrimitiveType::Cube);
} // Engine
