#pragma once

//============================================================================
//	include
//============================================================================
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
		bool DrawModule(ParticleModuleRegistry::TypeID typeID, IParticleModule& module) const;
		// 発生形状の編集UIを描画する
		bool DrawEmitterShape(ParticleEmitterShape shape, const IParticleEmitterShape& emitterShape,
			ParticleEmitterSettings& settings) const;

		// シングルトン
		static ParticleEditorDescriptorRegistry& GetInstance();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		using ModuleDrawFunc = bool(*)(IParticleModule&);
		using EmitterDrawFunc = bool(*)(const IParticleEmitterShape&, ParticleEmitterSettings&);

		ParticleEditorDescriptorRegistry();

		//--------- variables ----------------------------------------------------

		std::vector<ModuleDrawFunc> moduleDrawers_{};
		std::array<EmitterDrawFunc, kParticleEmitterShapeCount> emitterDrawers_{};
	};
} // Engine
