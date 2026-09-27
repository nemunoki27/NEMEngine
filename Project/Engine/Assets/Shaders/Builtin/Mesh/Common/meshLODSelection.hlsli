#ifndef NEM_MESH_LOD_SELECTION_HLSLI
#define NEM_MESH_LOD_SELECTION_HLSLI

// 描画先Cameraの投影サイズから共通のLODを選ぶ
uint ResolveMeshLOD(MeshInstance instance) {

	if (lodCount <= 1u || (instance.flags & MESH_INSTANCE_FLAG_SKINNED) != 0u) {
		return 0u;
	}
	float3 center = mul(float4(meshBoundsCenter, 1.0f), instance.worldMatrix).xyz;
	float radius = meshBoundsRadius * GetMatrixMaxScale(instance.worldMatrix);
	float2 pixelRadii = CalcProjectedPixelRadiusXY(viewProjection, lodView, lodNearClip,
		lodProjectionScale, viewSize, contributionPixelThreshold, center, radius);
	float pixelRadius = max(pixelRadii.x, pixelRadii.y);
	if (pixelRadius >= lodPixelThresholds.x) {
		return 0u;
	}
	if (pixelRadius >= lodPixelThresholds.y) {
		return min(1u, lodCount - 1u);
	}
	if (pixelRadius >= lodPixelThresholds.z) {
		return min(2u, lodCount - 1u);
	}
	return min(3u, lodCount - 1u);
}

#endif // NEM_MESH_LOD_SELECTION_HLSLI
