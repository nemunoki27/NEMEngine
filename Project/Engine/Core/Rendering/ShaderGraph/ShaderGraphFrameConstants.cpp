#include "ShaderGraphFrameConstants.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <algorithm>
#include <cmath>
#include <cstring>

Engine::ShaderGraphTimeConstantsGPU Engine::BuildShaderGraphFrameConstants(
	const SystemContext& system, const ResolvedRenderView& view, RenderCameraDomain domain, Vector4 viewport) {

	ShaderGraphTimeConstantsGPU result{};
	result.time = system.time;
	result.deltaTime = system.deltaTime;
	result.smoothDeltaTime = system.smoothDeltaTime;
	result.unscaledTime = system.unscaledTime;
	result.viewport = 0.0f < viewport.z && 0.0f < viewport.w ? viewport :
		Vector4(0.0f, 0.0f, static_cast<float>(view.width), static_cast<float>(view.height));
	result.screenSize.x = result.viewport.z;
	result.screenSize.y = result.viewport.w;
	const ResolvedCameraView* camera = view.FindCamera(domain);
	if (!camera) {
		return result;
	}

	const ViewProjectionMatrices& matrices = camera->matrices;
	result.worldToView = matrices.viewMatrix;
	result.viewToWorld = matrices.inverseViewMatrix;
	result.viewToClip = matrices.projectionMatrix;
	result.clipToView = matrices.inverseProjectionMatrix;
	result.worldToClip = matrices.viewProjectionMatrix;
	result.clipToWorld = matrices.inverseProjectionMatrix * matrices.inverseViewMatrix;
	result.cameraPosition = {camera->cameraPos.x, camera->cameraPos.y, camera->cameraPos.z, 1.0f};
	result.cameraDirection = {camera->forward.x, camera->forward.y, camera->forward.z, 0.0f};
	const bool orthographic = camera->projectionMode == ResolvedProjectionMode::Orthographic;
	result.cameraProjection = {orthographic ? 1.0f : 0.0f, camera->nearClip, camera->farClip, 1.0f};
	result.screenSize.z = orthographic ? 2.0f / (std::max)(std::abs(matrices.projectionMatrix.m[0][0]), 0.000001f) : 0.0f;
	result.screenSize.w = orthographic ? 2.0f / (std::max)(std::abs(matrices.projectionMatrix.m[1][1]), 0.000001f) : 0.0f;
	return result;
}

void Engine::ShaderGraphFrameConstantBuffer::BeginFrame() {

	allocator_.BeginFrame();
	entries_.clear();
}

void Engine::ShaderGraphFrameConstantBuffer::Release() {

	entries_.clear();
	allocator_.Release();
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::ShaderGraphFrameConstantBuffer::GetAddress(GraphicsCore& graphicsCore,
	const SystemContext& system, const ResolvedRenderView& view, RenderCameraDomain domain, Vector4 viewport) {

	const auto constants = BuildShaderGraphFrameConstants(system, view, domain, viewport);
	const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const FrameEntry& entry) {
		return std::memcmp(&entry.constants, &constants, sizeof(constants)) == 0;
	});
	if (found != entries_.end()) {
		return found->address;
	}

	auto& platform = graphicsCore.GetDXObject();
	const auto allocation = allocator_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(),
		constants);
	entries_.push_back({constants, allocation.gpuAddress});
	return allocation.gpuAddress;
}
