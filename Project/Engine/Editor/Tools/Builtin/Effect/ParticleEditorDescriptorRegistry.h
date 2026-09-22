#pragma once

//============================================================================
//	include
//============================================================================
#include "Modules/IParticleModuleDrawer.h"
#include <Engine/Core/Rendering/Particle/Module/Base/ParticleModuleRegistry.h>
#include <Engine/Core/Rendering/Particle/Emitter/Base/ParticleEmitterShapeRegistry.h>

// c++
#include <array>
#include <vector>

namespace Engine {

	//============================================================================
	//	ParticleEditorDescriptorRegistry class
	//	Particle Runtimeの型記述子へEditor専用の編集UIを関連付ける
	//============================================================================
	class ParticleEditorDescriptorRegistry {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// モジュールの編集UIを描画する
		bool DrawModule(ParticleModuleRegistry::TypeID typeID, IParticleModule& module, IParticleModuleDrawer* drawer) const;
		// モジュールごとの編集状態を作成する
		std::unique_ptr<IParticleModuleDrawer> CreateModuleDrawer(ParticleModuleRegistry::TypeID typeID) const;
		// 発生形状の編集UIを描画する
		bool DrawEmitterShape(ParticleEmitterShape shape, const IParticleEmitterShape& emitterShape,
			ParticleEmitterSettings& settings) const;

		// シングルトン
		static ParticleEditorDescriptorRegistry& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		using ModuleDrawerFactory = std::unique_ptr<IParticleModuleDrawer>(*)();
		using EmitterDrawFunc = bool(*)(const IParticleEmitterShape&, ParticleEmitterSettings&);

		ParticleEditorDescriptorRegistry();

		//--------- variables ----------------------------------------------------

		std::vector<ModuleDrawerFactory> moduleDrawerFactories_{};
		std::array<EmitterDrawFunc, kParticleEmitterShapeCount> emitterDrawers_{};
	};
} // Engine
