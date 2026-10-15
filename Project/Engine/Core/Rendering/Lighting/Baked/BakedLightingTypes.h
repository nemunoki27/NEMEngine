#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Vector4.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	BakedLighting structures
	//	ベイク用頂点と保存済みの照明データ
	//============================================================================
	struct BakedLightingVertex {

		Vector3 position{};
		float lightmapU = 0.0f;
		Vector3 normal{};
		float lightmapV = 0.0f;
		Vector2 uv{};
		uint32_t tangentOct = 0;
		float tangentSign = 1.0f;
	};
	struct BakedLightingTriangle {

		std::array<BakedLightingVertex, 3> vertices;
		uint32_t materialIndex = 0;
		uint32_t atlasIndex = 0;
		uint32_t chartIndex = 0;
		uint32_t receiverIndex = 0;
	};
	struct BakedLightingNode {

		Vector3 minimum{};
		uint32_t left = 0;
		Vector3 maximum{};
		uint32_t right = 0;
	};
	struct BakedLightingTexel {

		Vector3 position{};
		uint32_t valid = 0;
		Vector3 normal{};
		uint32_t chart = UINT32_MAX;
	};
	struct BakedLightingProbe {

		Vector3 position{};
		float radius = 0.0f;
		std::array<Vector4, 9> irradiance{};
		std::array<Vector4, 6> visibility{};
	};
	struct BakedReflectionCapture {

		Vector3 position{};
		float radius = 0.0f;
		Vector3 extents{};
		uint32_t shape = 0;
		uint32_t dataOffset = 0;
		uint32_t size = 0;
		uint32_t mipCount = 0;
		float intensity = 1.0f;
		std::array<std::array<float, 4>, 4> worldToBox = {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};
	};
	struct BakedLightingReceiver {

		UUID localFileID{};
		Matrix4x4 worldMatrix{};
		std::string settings;
	};
	struct BakedLightingData {

		AssetID sceneAsset{};
		uint32_t atlasSize = 0;
		uint32_t atlasCount = 0;
		float surfaceTolerance = 0.05f;
		float probeSpacing = 4.0f;
		Vector3 probeMinimum{};
		std::array<uint32_t, 3> probeDimensions{};
		std::vector<BakedLightingTriangle> triangles;
		std::vector<BakedLightingNode> nodes;
		std::vector<uint32_t> triangleOrder;
		std::vector<Vector4> lightmaps;
		std::vector<BakedLightingProbe> probes;
		std::vector<BakedReflectionCapture> captures;
		std::vector<Vector4> reflectionPixels;
		std::vector<BakedLightingReceiver> receivers;
		nlohmann::json inputRevisions = nlohmann::json::object();
	};
	struct BakedLightingSettings {

		uint32_t atlasSize = 256;
		uint32_t sampleCount = 128;
		uint32_t bounceCount = 4;
		uint32_t texelsPerFrame = 4096;
		float probeSpacing = 4.0f;
		uint32_t captureSize = 32;
		float normalBias = 0.01f;
	};
	static_assert(sizeof(BakedLightingVertex) == 48);
	static_assert(sizeof(BakedLightingTriangle) == 160);
	static_assert(sizeof(BakedLightingNode) == 32);
	static_assert(sizeof(BakedLightingTexel) == 32);
	static_assert(sizeof(BakedLightingProbe) == 256);
	static_assert(sizeof(BakedReflectionCapture) == 112);
}
