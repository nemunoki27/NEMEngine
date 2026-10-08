#ifndef NEM_CULLING_HELPERS_HLSLI
#define NEM_CULLING_HELPERS_HLSLI

//============================================================================
//	Culling Helpers
//============================================================================

// ビュー錐台の平面を取得
float4 GetFrustumPlane(float4x4 cullingViewProj, uint index) {

	float4 col0 = float4(cullingViewProj[0][0], cullingViewProj[1][0], cullingViewProj[2][0], cullingViewProj[3][0]);
	float4 col1 = float4(cullingViewProj[0][1], cullingViewProj[1][1], cullingViewProj[2][1], cullingViewProj[3][1]);
	float4 col2 = float4(cullingViewProj[0][2], cullingViewProj[1][2], cullingViewProj[2][2], cullingViewProj[3][2]);
	float4 col3 = float4(cullingViewProj[0][3], cullingViewProj[1][3], cullingViewProj[2][3], cullingViewProj[3][3]);

	if (index == 0) { return col3 + col0; }
	if (index == 1) { return col3 - col0; }
	if (index == 2) { return col3 + col1; }
	if (index == 3) { return col3 - col1; }
	if (index == 4) { return col2; }
	return col3 - col2;
}

// 平面式の正規化
float4 NormalizePlane(float4 plane) {

	float len = length(plane.xyz);
	if (len <= 0.00001f) {
		return plane;
	}
	return plane / len;
}

// せん断を含む変換でも球を包むスケールを求める
float GetMatrixMaxScale(float4x4 inputMat) {

	float3 x = inputMat[0].xyz;
	float3 y = inputMat[1].xyz;
	float3 z = inputMat[2].xyz;
	float xy = abs(dot(x, y));
	float xz = abs(dot(x, z));
	float yz = abs(dot(y, z));
	// Gram行列の最大行和で最大伸長率を上から抑える
	float3 sums = float3(dot(x, x) + xy + xz, dot(y, y) + xy + yz, dot(z, z) + xz + yz);
	return sqrt(max(sums.x, max(sums.y, sums.z)));
}

// 球内の視線差を含めて法線Coneの可視性を判定する
bool IsNormalConeSphereVisible(float3 axis, float cutoff, float3 cameraFromCenter, float radius) {

	const float distance = length(cameraFromCenter);
	if (cutoff < 0.5f || distance <= radius || distance <= 0.00001f) {
		return true;
	}
	const float viewSin = saturate(radius / distance);
	// 法線と視線の広がりが直角以上なら背面と断定しない
	if (viewSin >= cutoff) {
		return true;
	}
	const float coneSin = sqrt(saturate(1.0f - cutoff * cutoff));
	const float combinedSin = coneSin * sqrt(saturate(1.0f - viewSin * viewSin)) + cutoff * viewSin;
	return dot(axis, cameraFromCenter / distance) >= -combinedSin - 0.00001f;
}

// 視線を元の空間へ戻し、せん断と反転を含むConeを判定する
bool IsTransformedNormalConeVisible(float3 axis, float cutoff, float3 cameraFromCenter, float radius, float3x3 worldMatrix) {

	float3 inverseX = cross(worldMatrix[1], worldMatrix[2]);
	float3 inverseY = cross(worldMatrix[2], worldMatrix[0]);
	float3 inverseZ = cross(worldMatrix[0], worldMatrix[1]);
	const float orientation = dot(worldMatrix[0], inverseX);
	if (!isfinite(orientation) || abs(orientation) <= 0.00000001f) {
		return true;
	}
	// 合成後の逆変換を使い、法線行列の退化時補完に依存させない
	float3 localView = float3(dot(cameraFromCenter, inverseX), dot(cameraFromCenter, inverseY),
		dot(cameraFromCenter, inverseZ)) / orientation;
	if (any(!isfinite(localView))) {
		return true;
	}
	axis *= orientation < 0.0f ? -1.0f : 1.0f;
	return IsNormalConeSphereVisible(axis, cutoff, localView, radius);
}

// 球が錐台内にあるか判定
bool IsSphereInFrustum(float4x4 cullingViewProj, float3 center, float radius) {

	[unroll]
	for (uint i = 0; i < 6; ++i) {

		float4 plane = NormalizePlane(GetFrustumPlane(cullingViewProj, i));
		if (dot(plane.xyz, center) + plane.w < -radius) {
			return false;
		}
	}
	return true;
}

// 投影されたピクセル半径を計算
float2 CalcProjectedPixelRadiusXY(float4x4 cullingViewProj, float4x4 cullingView, float cullingNearClip,
	float2 cullingProjectionScale, float2 cullingViewSize, float contributionPixelThreshold,
	float3 center, float radius, bool orthographic) {

	float4 clip = mul(float4(center, 1.0f), cullingViewProj);
	if (clip.w <= 0.00001f) {
		return float2(contributionPixelThreshold, contributionPixelThreshold);
	}

	float3 viewCenter = mul(float4(center, 1.0f), cullingView).xyz;
	float nearZ = viewCenter.z - radius;
	if (nearZ <= max(cullingNearClip, 0.00001f)) {
		return float2(1000000.0f, 1000000.0f);
	}

	// 平行投影では奥行きで半径を縮めない
	float projectionDepth = orthographic ? 1.0f : nearZ;
	float2 projectedRadius = abs(radius * cullingProjectionScale / projectionDepth);
	return projectedRadius * cullingViewSize * 0.5f;
}

// 投影行列のW成分からカリングCameraの投影方式を選ぶ
float2 CalcProjectedPixelRadiusXY(float4x4 cullingViewProj, float4x4 cullingView, float cullingNearClip,
	float2 cullingProjectionScale, float2 cullingViewSize, float contributionPixelThreshold,
	float3 center, float radius) {

	float3 projectionW = float3(cullingViewProj[0][3], cullingViewProj[1][3], cullingViewProj[2][3]);
	bool orthographic = dot(projectionW, projectionW) <= 0.0000000001f;
	return CalcProjectedPixelRadiusXY(cullingViewProj, cullingView, cullingNearClip, cullingProjectionScale,
		cullingViewSize, contributionPixelThreshold, center, radius, orthographic);
}

// 球の画面矩形とカメラに最も近い深度をHi-Z判定用に求める
bool CalcSphereOcclusionProjection(
	float4x4 cullingViewProj,
	float4x4 cullingView,
	float cullingNearClip,
	float2 cullingProjectionScale,
	float2 cullingViewSize,
	float3 cullingCameraForward,
	float3 center,
	float radius,
	out float2 uvMin,
	out float2 uvMax,
	out float nearestDepth) {

	nearestDepth = 0.0f;
	const float3 viewCenter =
		mul(float4(center, 1.0f), cullingView).xyz;
	if (viewCenter.z - radius <=
		max(cullingNearClip, 0.00001f)) {
		return false;
	}

	const float4 centerClip =
		mul(float4(center, 1.0f), cullingViewProj);
	if (centerClip.w <= 0.00001f) {
		return false;
	}

	const float2 pixelRadius = CalcProjectedPixelRadiusXY(
		cullingViewProj, cullingView, cullingNearClip,
		cullingProjectionScale, cullingViewSize, 0.0f,
		center, radius);
	const float2 centerUV =
		centerClip.xy / centerClip.w *
		float2(0.5f, -0.5f) + 0.5f;
	const float2 uvRadius =
		pixelRadius / max(cullingViewSize, 1.0f) *
		// 画面端では奥行差による投影中心のずれも含める
		(1.0f + abs(viewCenter.xy) / viewCenter.z);
	const float2 unclampedMin = centerUV - uvRadius;
	const float2 unclampedMax = centerUV + uvRadius;
	if (unclampedMax.x <= 0.0f ||
		unclampedMax.y <= 0.0f ||
		unclampedMin.x >= 1.0f ||
		unclampedMin.y >= 1.0f) {
		return false;
	}
	uvMin = saturate(unclampedMin);
	uvMax = saturate(unclampedMax);

	// 深度はカメラ距離ではなくView-Zで決まるため前方軸へ球半径分寄せる
	const float3 cameraForward =
		normalize(cullingCameraForward);
	const float3 nearestPoint =
		center - cameraForward * radius;
	const float4 nearestClip =
		mul(float4(nearestPoint, 1.0f),
			cullingViewProj);
	if (nearestClip.w <= 0.00001f) {
		return false;
	}
	nearestDepth = nearestClip.z / nearestClip.w;
	return nearestDepth > 0.0f &&
		nearestDepth < 1.0f;
}

// 四隅だけで投影範囲を覆えるMipとtexel範囲を求める
bool ResolveHiZSampleBounds(uint2 baseSize, uint mipCount, float2 uvMin, float2 uvMax,
	out uint mipIndex, out uint2 pixelMin, out uint2 pixelMax) {

	mipIndex = 0u;
	pixelMin = pixelMax = uint2(0u, 0u);
	if (any(baseSize == 0u) || mipCount == 0u) {
		return false;
	}
	const float2 rectangleSize = (uvMax - uvMin) * float2(baseSize);
	const float maxExtent = max(rectangleSize.x, rectangleSize.y);
	// 切り上げにより各軸の範囲を最大2texelへ収める
	mipIndex = min((uint)max(0.0f, ceil(log2(max(maxExtent, 1.0f)))), min(mipCount - 1u, 31u));
	const uint2 mipSize = max(baseSize >> mipIndex, uint2(1u, 1u));
	const uint2 maxPixel = mipSize - 1u;
	// 奇数サイズでも2倍縮小時の元pixel境界へ合わせる
	const float scale = exp2((float)mipIndex);
	pixelMin = min((uint2)(uvMin * float2(baseSize) / scale), maxPixel);
	pixelMax = min((uint2)(uvMax * float2(baseSize) / scale), maxPixel);
	// Mip不足や丸め誤差で覆い切れない場合は遮蔽しない
	return all(pixelMax <= pixelMin + 1u);
}

#ifdef NEM_OCCLUSION_DEPTH_PYRAMID
// 球の投影矩形を覆うHi-Z Mipから遮蔽を判定する
bool IsSphereOccludedHiZ(
	float4x4 cullingViewProj,
	float4x4 cullingView,
	float cullingNearClip,
	float2 cullingProjectionScale,
	float2 cullingViewSize,
	float3 cullingCameraForward,
	float3 center,
	float radius) {

	float2 uvMin;
	float2 uvMax;
	float nearestDepth;
	if (!CalcSphereOcclusionProjection(
		cullingViewProj, cullingView,
		cullingNearClip, cullingProjectionScale,
		cullingViewSize, cullingCameraForward,
		center, radius, uvMin, uvMax,
		nearestDepth)) {
		return false;
	}

	uint width;
	uint height;
	uint mipCount;
	NEM_OCCLUSION_DEPTH_PYRAMID.GetDimensions(
		0, width, height, mipCount);
	uint mipIndex;
	uint2 pixelMin;
	uint2 pixelMax;
	if (!ResolveHiZSampleBounds(uint2(width, height), mipCount, uvMin, uvMax,
		mipIndex, pixelMin, pixelMax)) {
		return false;
	}

	float maxDepth =
		NEM_OCCLUSION_DEPTH_PYRAMID.Load(
			int3(pixelMin, mipIndex));
	maxDepth = max(maxDepth,
		NEM_OCCLUSION_DEPTH_PYRAMID.Load(
			int3(uint2(pixelMax.x, pixelMin.y),
				mipIndex)));
	maxDepth = max(maxDepth,
		NEM_OCCLUSION_DEPTH_PYRAMID.Load(
			int3(uint2(pixelMin.x, pixelMax.y),
				mipIndex)));
	maxDepth = max(maxDepth,
		NEM_OCCLUSION_DEPTH_PYRAMID.Load(
			int3(pixelMax, mipIndex)));
	// 未生成Mipや無効値を遮蔽面として扱わない
	return maxDepth > 0.000001f &&
		nearestDepth > maxDepth + 0.0005f;
}
#endif

#endif // NEM_CULLING_HELPERS_HLSLI
