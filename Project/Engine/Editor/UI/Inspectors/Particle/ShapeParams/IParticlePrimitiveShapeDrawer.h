#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>

namespace Engine {

	//============================================================================
	//	IParticlePrimitiveShapeDrawer class
	//	描画形状ごとのパラメータ編集UIのインターフェース、状態を持たず共有される
	//============================================================================
	class IParticlePrimitiveShapeDrawer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IParticlePrimitiveShapeDrawer() = default;
		virtual ~IParticlePrimitiveShapeDrawer() = default;

		// 形状パラメータの編集UIを描画する、変更があればtrue
		virtual bool DrawImGui(ParticleSystemSettings& group) const = 0;
	};
} // Engine
