#pragma once

//============================================================================
//	include
//============================================================================
#include "UIInputTypes.h"
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>

namespace Engine::UISelectableStyle {

	MaterialParameterSet* ResolveMaterialParameters( ECSWorld& world, Entity target);

	bool SetMaterialParameter(MaterialParameterSet& parameters, MaterialParameterID id, std::string_view name,
		MaterialParameterSemantic semantic, const MaterialParameterValue& value);

	void NotifyRendererMaterialModified(ECSWorld& world);

	const UITransitionStyle& ResolveStyle( const UISelectableComponent& selectable,
		const UISelectableRuntimeComponent& runtime);

	size_t GetStyleIndex(UISelectableState state);

	std::array<const UITransitionStyle*, 4> GetStyles( const UISelectableComponent& selectable);

	bool HasStateTransitionRuntime(const UISelectableComponent& selectable);

	void ApplySelectableBaseVisual(ECSWorld& world, Entity entity, UISelectableRuntimeComponent& runtime);

	void RestoreSelectableVisual(ECSWorld& world, Entity entity, UISelectableRuntimeComponent& runtime);

	void ResetStateThisFrame(UISelectableRuntimeComponent& runtime);

	void SetStateThisFrame(UISelectableRuntimeComponent& runtime, UISelectableState state);

	void InitializeSelectableRuntime(ECSWorld& world, Entity entity, const UISelectableComponent& selectable,
		UISelectableRuntimeComponent& runtime);

	void UpdateSelectableVisual(ECSWorld& world, const UISelectableEntry& entry, float deltaTime);
}
