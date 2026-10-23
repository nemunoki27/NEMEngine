#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleDefinition.h"

// c++
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ParticleDefinitionCompiler class
	//	変更不可の設定から、共有する実行定義を作る
	//============================================================================
	class ParticleDefinitionCompiler {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 同じSnapshotの実行定義を再利用する
		std::shared_ptr<const ParticleSystemDefinition> Resolve(std::shared_ptr<const ParticleSystemSettings> settings);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<const ParticleSystemSettings*, std::weak_ptr<const ParticleSystemDefinition>> definitions_;

		//--------- functions ----------------------------------------------------

		// PhaseとModuleの登録順を実行用へ変換する
		std::shared_ptr<const ParticleSystemDefinition> Compile(std::shared_ptr<const ParticleSystemSettings> settings) const;
	};
}
