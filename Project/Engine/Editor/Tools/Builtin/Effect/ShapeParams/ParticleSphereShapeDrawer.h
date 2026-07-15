#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticlePrimitiveShapeDrawerRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleSphereShapeDrawer class
	//	Sphere形状のパラメータ編集UI
	//============================================================================
	class ParticleSphereShapeDrawer :
		public IParticlePrimitiveShapeDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleSphereShapeDrawer() = default;
		~ParticleSphereShapeDrawer() override = default;

		bool DrawImGui(ParticleEffectGroup& group) const override;
	};

	ENGINE_REGISTER_PARTICLE_PRIMITIVE_SHAPE_DRAWER(ParticleSphereShapeDrawer, PrimitiveType::Sphere);
} // Engine
