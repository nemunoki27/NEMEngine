#include "RaytracingViewBufferSet.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Utility/Enum/EnumAdapter.h>

// imgui
#include <imgui.h>

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

void Engine::RaytracingViewBufferSet::Upload(const ResolvedRenderView& view) {

	/*ImGui::Begin("RaytracingViewBuffer");

	ImGui::PushID(EnumAdapter<RenderViewKind>::GetIndex(view.kind));

	ImGui::Text("RenderViewKind: %s", EnumAdapter<RenderViewKind>::ToString(view.kind));

	ImGui::DragFloat("MaxReflectionDistance", &debugData_.maxReflectionDistance, 1.0f, 0.0f, 10000.0f);
	ImGui::DragFloat("ShadowNormalBias", &debugData_.shadowNormalBias, 0.001f, 0.0f);
	ImGui::DragFloat("ReflectionIntensity", &debugData_.reflectionIntensity, 0.01f);
	ImGui::DragFloat("reflectionNormalBias", &debugData_.reflectionNormalBias, 0.01f);
	ImGui::DragFloat("reflectionViewBias", &debugData_.reflectionViewBias, 0.01f);
	ImGui::DragFloat("reflectionMinHitDistance", &debugData_.reflectionMinHitDistance, 0.01f);
	ImGui::DragFloat("reflectionThicknessBase", &debugData_.reflectionThicknessBase, 0.01f);
	ImGui::DragFloat("skyIntensity", &debugData_.skyIntensity, 0.01f);
	ImGui::DragFloat("reflectionThicknessScale", &debugData_.reflectionThicknessScale, 0.01f);
	ImGui::DragFloat("fresnelMin", &debugData_.fresnelMin, 0.01f);

	ImGui::PopID();

	ImGui::End();*/

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

	// データ転送
	params_.Upload(debugData_);
}

void Engine::RaytracingViewBufferSet::RegisterTo(RenderBufferRegistry& registry) const {

	registry.Register({ .alias = "RaytracingViewConstants",.resource = nullptr,.gpuAddress = params_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(RaytracingViewConstantsGPU) });
	registry.Register({ .alias = "gRaytracingViewConstants",.resource = nullptr,.gpuAddress = params_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(RaytracingViewConstantsGPU) });
}