#include "RaytracingViewBufferSet.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SceneSkyboxResolver.h>

//============================================================================
//	RaytracingViewBufferSet classMethods
//============================================================================
void Engine::RaytracingViewBufferSet::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	// 定数バッファを初期化
	params_.Init(graphicsCore.GetDXObject().GetDevice());
	initialized_ = true;
}

void Engine::RaytracingViewBufferSet::Upload(const ResolvedRenderView& view, const SceneSkyboxInfo& skybox) {


	// 描画サイズとその逆数を転送
	debugData_.renderSize = Vector2(static_cast<float>((std::max)(view.width, 1u)), static_cast<float>((std::max)(view.height, 1u)));
	debugData_.invRenderSize = Vector2(1.0f / debugData_.renderSize.x, 1.0f / debugData_.renderSize.y);

	const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective);
	// カメラ情報が有効な場合は行列やカメラ位置、クリップ距離を転送
	if (camera) {

		debugData_.view = camera->matrices.viewMatrix;
		debugData_.projection = camera->matrices.projectionMatrix;
		debugData_.inverseView = camera->matrices.inverseViewMatrix;
		debugData_.inverseProjection = camera->matrices.inverseProjectionMatrix;
		debugData_.inverseViewProjection = Matrix4x4::Inverse(camera->matrices.viewProjectionMatrix);
		debugData_.cameraPos = camera->cameraPos;
		debugData_.nearClip = camera->nearClip;
		debugData_.farClip = camera->farClip;
	}

	// 反射レイのミス時に参照するskybox情報を設定
	debugData_.skyboxColor = skybox.color;
	debugData_.skyboxCubemapIndex = skybox.cubemapIndex;
	debugData_.hasSkybox = skybox.found ? 1u : 0u;
	debugData_.iblIntensity = skybox.iblIntensity;
	debugData_.frameIndex = static_cast<uint32_t>(
		GraphicsFrameState::GetFrameSerial());

	// データ転送
	params_.Upload(debugData_);
}

void Engine::RaytracingViewBufferSet::RegisterTo(RenderBufferRegistry& registry) const {

	registry.Register({ .alias = "RaytracingViewConstants",.resource = nullptr,.gpuAddress = params_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(RaytracingViewConstantsGPU) });
	registry.Register({ .alias = "gRaytracingViewConstants",.resource = nullptr,.gpuAddress = params_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(RaytracingViewConstantsGPU) });
}
