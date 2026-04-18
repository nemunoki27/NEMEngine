#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Matrix4x4.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	LightCullingGpuTypes structures
	//============================================================================

	// 1タイルあたりのライトリスト情報
	struct TileLightGridEntryGPU {

		// インデックスリストの開始位置
		uint32_t offset = 0;

		// このタイルに影響を与えるライトの数
		uint32_t count = 0;

		// ライト数
		uint32_t pointCount = 0;
		uint32_t spotCount = 0;
	};

	// ライトカリングに必要なパラメータ
	struct LightCullingParamsGPU {

		// ビュー/プロジェクション行列
		Matrix4x4 viewMatrix = Matrix4x4::Identity();
		Matrix4x4 projectionMatrix = Matrix4x4::Identity();
		Matrix4x4 inverseProjectionMatrix = Matrix4x4::Identity();

		// スクリーンサイズとタイルサイズ
		uint32_t screenWidth = 0;
		uint32_t screenHeight = 0;
		uint32_t tileSizeX = 16;
		uint32_t tileSizeY = 16;

		// タイル数
		uint32_t tileCountX = 0;
		uint32_t tileCountY = 0;
		uint32_t totalTileCount = 0;
		uint32_t maxLocalLightsPerTile = 64;

		// ライト数
		uint32_t pointLightCount = 0;
		uint32_t spotLightCount = 0;
		uint32_t localLightCount = 0;
		uint32_t _pad0 = 0;

		float nearClip = 0.1f;
		float farClip = 1000.0f;
		float _pad1[2] = { 0.0f, 0.0f };
	};
}