#pragma once

//============================================================================
//	include
//============================================================================
#include "BakedLightingBake.h"
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/BottomLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/TopLevelAccelerationStructure.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingMaterialResolver.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>

// c++
#include <atomic>
#include <future>

namespace Engine {

	struct BakedLightingMaterialGPU {

		Color4 baseColor{};
		Color4 emissive{};
		uint32_t baseColorTexture = UINT32_MAX;
		uint32_t emissiveTexture = UINT32_MAX;
		uint32_t opacityTexture = UINT32_MAX;
		uint32_t masked = 0;
		float metallic = 0.0f;
		float roughness = 0.5f;
		float alphaCutoff = 0.5f;
		uint32_t metallicTexture = UINT32_MAX;
		uint32_t metallicRoughnessTexture = UINT32_MAX;
		uint32_t roughnessTexture = UINT32_MAX;
		uint32_t normalTexture = UINT32_MAX;
		float opacity = 1.0f;
	};
	struct BakedLightingBakeConstants {

		uint32_t stage = 0;
		uint32_t firstTask = 0;
		uint32_t taskCount = 0;
		uint32_t sampleStart = 0;
		uint32_t samplesPerTask = 4;
		uint32_t bounceCount = 4;
		uint32_t skyboxIndex = UINT32_MAX;
		float normalBias = 0.01f;
		Color4 skyColor = Color4::White();
		uint32_t lightmapCount = 0;
		uint32_t captureSize = 32;
		uint32_t captureCount = 0;
		float skyIntensity = 1.0f;
		float maximumDistance = 10000.0f;
		uint32_t padding[3]{};
	};
	struct BakedLightingBakeState {

		BakedLightingInput input;
		BakedLightingSettings settings;
		std::unique_ptr<AssetDatabase> database;
		std::atomic<bool> cancelled = false;
		std::atomic<float> preparationProgress = 0.0f;
		std::future<void> worker;
		std::shared_ptr<const BakedLightingData> result;
		std::string status;
		bool running = false;
		bool prepared = false;
		bool initialized = false;
		bool waitingReadback = false;
		bool filtering = false;
		uint32_t stage = 0;
		uint32_t firstTask = 0;
		uint32_t sampleStart = 0;
		uint32_t skyboxIndex = UINT32_MAX;
		TextureOwnership skyOwnership;
		uint64_t copyFrame = 0;
		uint32_t copySlot = 0;
		uint64_t copyFence = UINT64_MAX;
		GraphicsResourceRetirement* retirement = nullptr;
		RaytracingMaterialResolver materialResolver;
		MeshStructuredHandle<BakedLightingTriangle> triangles;
		MeshStructuredHandle<BakedLightingTexel> texels;
		MeshStructuredHandle<Vector3> positions;
		MeshStructuredHandle<uint32_t> indices;
		MeshStructuredHandle<BakedLightingMaterialGPU> materials;
		MeshStructuredHandle<BakedLightingProbe> probes;
		MeshStructuredHandle<BakedReflectionCapture> captures;
		MeshStructuredHandle<DirectionalLightGPU> directionalLights;
		MeshStructuredHandle<PointLightGPU> pointLights;
		MeshStructuredHandle<SpotLightGPU> spotLights;
		MeshStructuredHandle<RectLightGPU> rectLights;
		BottomLevelAccelerationStructure blas;
		TopLevelAccelerationStructure tlas;
		StructuredRWBuffer<Vector4> output;
		StructuredRWBuffer<BakedLightingProbe> probeOutput;
		ComPtr<ID3D12Resource> readback;
		ComPtr<ID3D12Resource> probeReadback;
		ViewConstantBuffer<BakedLightingBakeConstants> constants;
		ViewConstantBuffer<LightCountsGPU> lightCounts;
		std::unique_ptr<PipelineState> pipeline;
		~BakedLightingBakeState();
	};

	// CPU準備後のProbe配置とCapture出力を確定する
	void PrepareBakedLightingVolumes(BakedLightingBakeState& state);
	// GPU資源とMaterialを揃える
	bool InitializeBakedLightingGPU(GraphicsCore& graphics, BakedLightingBakeState& state);
	// 小さい計算単位を記録する
	void DispatchBakedLightingGPU(GraphicsCore& graphics, BakedLightingBakeState& state);
	// 提出済みFenceを確認して結果を受け取る
	bool ReadBakedLightingGPU(GraphicsCore& graphics, BakedLightingBakeState& state);
	// 完了や取消後の作業資源をGPU完了後に回収する
	void ReleaseBakedLightingGPU(BakedLightingBakeState& state);
	// Captureの粗さ別Mipを生成する
	void FilterBakedReflectionCaptures(BakedLightingData& data, const std::atomic<bool>& cancelled);
}
