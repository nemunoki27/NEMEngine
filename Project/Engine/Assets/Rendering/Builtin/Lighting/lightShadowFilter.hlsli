#ifndef NEM_LIGHT_SHADOW_FILTER_HLSLI
#define NEM_LIGHT_SHADOW_FILTER_HLSLI

StructuredBuffer<uint> gLightShadowFilters : register(t30);

bool HasLightShadowExclusions(uint filterIndex) {

	if (filterIndex == 0xFFFFFFFFu) return false;
	return gLightShadowFilters[filterIndex * 2u + 1u] != 0u;
}

bool IsLightShadowInstanceExcluded(uint filterIndex, uint instanceID) {

	uint start = gLightShadowFilters[filterIndex * 2u];
	uint count = gLightShadowFilters[filterIndex * 2u + 1u];
	uint end = start + count;
	while (start < end) {
		uint middle = start + (end - start) / 2u;
		uint candidate = gLightShadowFilters[middle];
		if (candidate == instanceID) return true;
		if (candidate < instanceID) start = middle + 1u;
		else end = middle;
	}
	return false;
}

// 除外後に最初に受理した遮蔽物で走査を終了する
bool TraceFilteredLightShadow(RaytracingAccelerationStructure scene, RayDesc ray, uint mask,
	uint filterIndex, out float hitDistance) {

	RayQuery<RAY_FLAG_FORCE_NON_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
		RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
	query.TraceRayInline(scene, 0, mask, ray);
	while (query.Proceed()) {
		if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE &&
			!IsLightShadowInstanceExcluded(filterIndex, query.CandidateInstanceID())) {
			query.CommitNonOpaqueTriangleHit();
		}
	}
	bool hit = query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
	hitDistance = hit ? query.CommittedRayT() : 0.0f;
	return hit;
}

#endif
