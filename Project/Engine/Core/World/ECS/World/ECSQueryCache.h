#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/EntityArchetype.h>

// c++
#include <memory>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	ECSQueryCache class
	//	シグネチャごとのアーキタイプ検索計画を保持する
	//============================================================================
	class ECSQueryCache {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// アーキタイプの世代に合わせて検索計画を取得する
		const std::vector<EntityArchetype*>& Resolve(const EntitySignature& required,
			const std::unordered_map<EntitySignature, std::unique_ptr<EntityArchetype>, EntitySignatureHash>& archetypes,
			uint32_t archetypeVersion);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct MatchPlan {

			std::vector<EntityArchetype*> archetypes;
			uint32_t builtArchetypeVersion = 0xFFFFFFFFu;
		};

		//--------- variables ----------------------------------------------------

		// シグネチャごとの検索結果
		std::unordered_map<EntitySignature, MatchPlan, EntitySignatureHash> plans_;
	};
} // Engine
