#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleEffectDefinition.h"
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

// c++
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ParticleEffectDefinitionCache class
	//	エフェクトの共有実行定義と更新世代を所有する
	//============================================================================
	class ParticleEffectDefinitionCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// エフェクトを取得する、未ロードならアセットを読み込みフェーズを構築する
		const ParticleEffectDefinition* ResolveEffect(SystemContext& context, AssetID effectID, bool checkReload);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::unordered_map<AssetID, ParticleEffectDefinition> effectCache_;

		//--------- functions ----------------------------------------------------

		// アセットを読み込んでフェーズを構築する
		ParticleEffectDefinition LoadEffect(SystemContext& context, AssetID effectID) const;
		// アセットのグループとフェーズから実行定義を構築する
		void BuildGroups(ParticleEffectDefinition& runtime) const;
	};
}
