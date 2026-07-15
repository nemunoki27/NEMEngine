#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticlePrimitiveShapeDrawerRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleCylinderShapeDrawer class
	//	Cylinder形状のパラメータ編集UI
	//============================================================================
	class ParticleCylinderShapeDrawer :
		public IParticlePrimitiveShapeDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleCylinderShapeDrawer() = default;
		~ParticleCylinderShapeDrawer() override = default;

		bool DrawImGui(ParticleEffectGroup& group) const override;
	};

	ENGINE_REGISTER_PARTICLE_PRIMITIVE_SHAPE_DRAWER(ParticleCylinderShapeDrawer, PrimitiveType::Cylinder);
} // Engine
