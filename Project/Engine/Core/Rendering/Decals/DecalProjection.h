#pragma once

//============================================================================
//	include
//============================================================================
#include "DecalTypes.h"
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>

namespace Engine::DecalProjectionUtility {

	// 寸法と親変換から投影行列を作る
	bool Build(const DecalProjectorComponent& component, const Matrix4x4& world, DecalProjection& output);
	// 球Boundsと投影範囲の交差を保守的に判定する
	bool Intersects(const Matrix4x4& worldToDecal, const Vector3& center, float radius);
	// 近面を跨ぐ範囲を欠かさず画面矩形へ変換する
	bool GetScreenRect(const DecalProjection& projection, const Matrix4x4& viewProjection,
		uint32_t width, uint32_t height, int32_t& left, int32_t& top, int32_t& right, int32_t& bottom);
	// 距離に応じて不透明度を減衰する
	float DistanceFade(float distance, float drawDistance, float fadeScale);
}
