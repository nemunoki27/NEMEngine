#ifndef NEM_MESH_LOD_SELECTION_HLSLI
#define NEM_MESH_LOD_SELECTION_HLSLI

struct MeshLODSelection {

	uint firstLOD;
	uint secondLOD;
	float firstCoverage;
	float secondCoverage;
};

float ResolveMeshLODPixelRadius(MeshInstance instance) {

	float3 center = mul(float4(meshBoundsCenter, 1.0f), instance.worldMatrix).xyz;
	float radius = meshBoundsRadius * GetMatrixMaxScale(instance.worldMatrix);
	float2 pixelRadii = CalcProjectedPixelRadiusXY(viewProjection, lodView, lodNearClip,
		lodProjectionScale, viewSize, contributionPixelThreshold, center, radius);
	return max(pixelRadii.x, pixelRadii.y);
}

// 描画先Cameraの投影サイズからLODと遷移率を選ぶ
MeshLODSelection ResolveMeshLODSelection(MeshInstance instance) {

	MeshLODSelection result;
	result.firstLOD = 0u;
	result.secondLOD = 0xFFFFFFFFu;
	result.firstCoverage = 1.0f;
	result.secondCoverage = 0.0f;
	if (lodCount <= 1u || (instance.flags & MESH_INSTANCE_FLAG_SKINNED) != 0u) {
		return result;
	}

	float pixelRadius = ResolveMeshLODPixelRadius(instance);
	if (pixelRadius >= lodPixelThresholds.x) {
		result.firstLOD = 0u;
	} else if (pixelRadius >= lodPixelThresholds.y) {
		result.firstLOD = min(1u, lodCount - 1u);
	} else if (pixelRadius >= lodPixelThresholds.z) {
		result.firstLOD = min(2u, lodCount - 1u);
	} else {
		result.firstLOD = min(3u, lodCount - 1u);
	}
	if (lodDitherEnabled == 0u) {
		return result;
	}

	[unroll]
	for (uint boundary = 0u; boundary < 3u; ++boundary) {

		if (boundary + 1u >= lodCount) {
			break;
		}
		float threshold = lodPixelThresholds[boundary];
		float halfWidth = max(threshold * 0.1f, 1.0f);
		if (pixelRadius < threshold - halfWidth ||
			pixelRadius > threshold + halfWidth) {
			continue;
		}
		float coarseCoverage = saturate(
			(threshold + halfWidth - pixelRadius) /
			(halfWidth * 2.0f));
		result.firstLOD = boundary;
		result.secondLOD = boundary + 1u;
		result.firstCoverage = 1.0f - coarseCoverage;
		result.secondCoverage = coarseCoverage;
		break;
	}
	return result;
}

uint ResolveMeshLOD(MeshInstance instance) {

	return ResolveMeshLODSelection(instance).firstLOD;
}

#endif // NEM_MESH_LOD_SELECTION_HLSLI
