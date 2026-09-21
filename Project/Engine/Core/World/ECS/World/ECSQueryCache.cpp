#include "ECSQueryCache.h"

//============================================================================
//	include
//============================================================================

//============================================================================
//	include
//============================================================================

//============================================================================
//	include
//============================================================================

//============================================================================
//	include
//============================================================================

using namespace Engine;

//============================================================================
//	ECSQueryCache classMethods
//============================================================================
const std::vector<EntityArchetype*>& ECSQueryCache::Resolve(const EntitySignature& required,
	const std::unordered_map<EntitySignature, std::unique_ptr<EntityArchetype>, EntitySignatureHash>& archetypes,
	uint32_t archetypeVersion) {

	MatchPlan& plan = plans_[required];
	if (plan.builtArchetypeVersion != archetypeVersion) {

		plan.archetypes.clear();
		for (const auto& [signature, archetypePointer] : archetypes) {

			EntityArchetype* archetype = archetypePointer.get();
			if (archetype->GetSignature().Contains(required)) {
				plan.archetypes.emplace_back(archetype);
			}
		}
		plan.builtArchetypeVersion = archetypeVersion;
	}
	return plan.archetypes;
}
