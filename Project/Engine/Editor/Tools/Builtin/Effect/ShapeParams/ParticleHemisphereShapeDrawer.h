#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticlePrimitiveShapeDrawerRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleHemisphereShapeDrawer class
	//	Hemisphere形状のパラメータ編集UI
	//============================================================================
	class ParticleHemisphereShapeDrawer :
		public IParticlePrimitiveShapeDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleHemisphereShapeDrawer() = default;
		~ParticleHemisphereShapeDrawer() override = default;

		bool DrawImGui(ParticleEffectGroup& group) const override;
	};

	ENGINE_REGISTER_PARTICLE_PRIMITIVE_SHAPE_DRAWER(ParticleHemisphereShapeDrawer, PrimitiveType::Hemisphere);
} // Engine
