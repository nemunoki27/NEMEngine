#include "ViewLightBufferSet.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Raytracing/RaytracingSceneBuilder.h>

void Engine::ViewLightBufferSet::UpdateShadowFilters(const PerViewLightSet& lightSet,
	const RaytracingSceneBuilder* scene) {

	shadowFilterScratch_.assign(static_cast<size_t>(lightSet.GetTotalCount()) * 2u, 0u);
	uint32_t index = 0;
	const auto append = [&](const auto& lights) {

		for (const auto* light : lights) {

			const uint32_t offset = static_cast<uint32_t>(shadowFilterScratch_.size());
			if (scene && light->common.world) {
				scene->AppendShadowExclusions(light->common.influence, *light->common.world,
					light->common.entity, shadowFilterScratch_);
			}
			shadowFilterScratch_[index * 2u] = offset;
			shadowFilterScratch_[index * 2u + 1u] = static_cast<uint32_t>(shadowFilterScratch_.size()) - offset;
			++index;
		}
	};
	append(lightSet.directionalLights);
	append(lightSet.pointLights);
	append(lightSet.spotLights);
	append(lightSet.rectLights);
	if (shadowFilterScratch_ != shadowFilterData_) {
		shadowFilterData_.swap(shadowFilterScratch_);
		++data_.dataSerial_;
		++shadowFilterRevision_;
	}
}
