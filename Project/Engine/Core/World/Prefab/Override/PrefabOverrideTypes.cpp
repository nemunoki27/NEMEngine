#include "PrefabOverrideTypes.h"

//============================================================================
//	PrefabOverrideTypes classMethods
//============================================================================

namespace Engine {

	bool PrefabInstanceData::IsEmpty() const {

		return modifications.empty() && addedComponents.empty() && removedComponents.empty() &&
			hierarchyModifications.empty() && removedEntities.empty() && addedEntities.empty() &&
			nestedInstances.empty() && removedNestedSlots.empty() &&
			!rootParentSceneLocalFileID;
	}
}
