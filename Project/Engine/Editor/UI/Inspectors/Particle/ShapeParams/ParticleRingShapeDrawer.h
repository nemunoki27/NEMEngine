#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Particle/ShapeParams/ParticlePrimitiveShapeDrawerRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleRingShapeDrawer class
	//	Ring形状のパラメータ編集UI
	//============================================================================
	class ParticleRingShapeDrawer :
		public IParticlePrimitiveShapeDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleRingShapeDrawer() = default;
		~ParticleRingShapeDrawer() override = default;

		bool DrawImGui(ParticleSystemSettings& group) const override;
	};

	ENGINE_REGISTER_PARTICLE_PRIMITIVE_SHAPE_DRAWER(ParticleRingShapeDrawer, PrimitiveType::Ring);
} // Engine
