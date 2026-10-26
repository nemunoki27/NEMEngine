#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleGPUWorldResources.h"

// c++
#include <functional>

namespace Engine {

	class MeshRenderBackend;

	// 色と深度と行列を同じ描画時点で固定する
	struct ParticleGPUCameraInput {

		D3D12_GPU_DESCRIPTOR_HANDLE colorSRV{};
		D3D12_GPU_DESCRIPTOR_HANDLE depthSRV{};
		D3D12_GPU_DESCRIPTOR_HANDLE previousDepthSRV{};
		Matrix4x4 inverseViewProjection = Matrix4x4::Identity();
		Matrix4x4 previousInverseViewProjection = Matrix4x4::Identity();
		uint32_t width = 0;
		uint32_t height = 0;
		std::shared_ptr<const void> owner;
	};

	// View表示とは独立したGPU更新の描画入力
	struct ParticleGPUFrameInputs {

		MeshRenderBackend* meshes = nullptr;
		Entity mainCamera{};
		float deltaTime = 0;
		std::vector<UUID> sceneInstances;
		std::function<ParticleGPUDepthInput(Entity)> resolveDepth;
		std::function<ParticleGPUCameraInput(Entity)> resolveCamera;
	};
}
