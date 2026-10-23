#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Particle/ShapeParams/ParticlePrimitiveShapeDrawerRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticleCrossPlaneShapeDrawer class
	//	CrossPlane形状のパラメータ編集UI
	//============================================================================
	class ParticleCrossPlaneShapeDrawer :
		public IParticlePrimitiveShapeDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticleCrossPlaneShapeDrawer() = default;
		~ParticleCrossPlaneShapeDrawer() override = default;

		bool DrawImGui(ParticleSystemSettings& group) const override;
	};

	ENGINE_REGISTER_PARTICLE_PRIMITIVE_SHAPE_DRAWER(ParticleCrossPlaneShapeDrawer, PrimitiveType::CrossPlane);
} // Engine
