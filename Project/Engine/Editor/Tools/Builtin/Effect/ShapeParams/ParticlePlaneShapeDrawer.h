#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Effect/ShapeParams/ParticlePrimitiveShapeDrawerRegistry.h>

namespace Engine {

	//============================================================================
	//	ParticlePlaneShapeDrawer class
	//	Plane形状のパラメータ編集UI
	//============================================================================
	class ParticlePlaneShapeDrawer :
		public IParticlePrimitiveShapeDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ParticlePlaneShapeDrawer() = default;
		~ParticlePlaneShapeDrawer() override = default;

		bool DrawImGui(ParticleEffectAsset& asset) const override;
	};

	ENGINE_REGISTER_PARTICLE_PRIMITIVE_SHAPE_DRAWER(ParticlePlaneShapeDrawer, PrimitiveType::Plane);
} // Engine
