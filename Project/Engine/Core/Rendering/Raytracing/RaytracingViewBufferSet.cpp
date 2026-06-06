#include "RaytracingViewBufferSet.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

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

	/*{
		char windowTitle[64];
		snprintf(windowTitle, sizeof(windowTitle), "Raytracing Parameters (%s)", EnumAdapter<RenderViewKind>::ToString(view.kind));

		ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
		ImGui::Begin(windowTitle);
		ImGui::PushID(EnumAdapter<RenderViewKind>::GetIndex(view.kind));

		if (ImGui::CollapsingHeader("反射", ImGuiTreeNodeFlags_DefaultOpen)) {

			ImGui::DragFloat("最大反射距離",       &debugData_.maxReflectionDistance,    1.0f,    0.0f,    10000.0f, "%.1f");
			ImGui::DragFloat("反射強度",           &debugData_.reflectionIntensity,       0.01f,   0.0f,    4.0f,    "%.3f");
			ImGui::DragFloat("Fresnel最小値",      &debugData_.fresnelMin,                0.001f,  0.0f,    1.0f,    "%.4f");
			ImGui::DragFloat("スカイ強度",         &debugData_.skyIntensity,              0.01f,   0.0f,    8.0f,    "%.3f");
		}

		if (ImGui::CollapsingHeader("レイ安定化", ImGuiTreeNodeFlags_DefaultOpen)) {

			ImGui::DragFloat("法線バイアス(反射)", &debugData_.reflectionNormalBias,      0.00001f, 0.0f,  0.1f,    "%.6f");
			ImGui::DragFloat("ビューバイアス(反射)",&debugData_.reflectionViewBias,       0.00001f, 0.0f,  0.1f,    "%.6f");
			ImGui::DragFloat("最小ヒット距離",     &debugData_.reflectionMinHitDistance,  0.0001f, 0.0f,  1.0f,    "%.5f");
			ImGui::DragFloat("厚みベース",         &debugData_.reflectionThicknessBase,   0.001f,  0.0f,  2.0f,    "%.4f");
			ImGui::DragFloat("厚みスケール",       &debugData_.reflectionThicknessScale,  0.0001f, 0.0f,  0.5f,    "%.5f");
		}

		if (ImGui::CollapsingHeader("シャドウ", ImGuiTreeNodeFlags_DefaultOpen)) {

			ImGui::DragFloat("法線バイアス(影)",   &debugData_.shadowNormalBias,          0.0001f, 0.0f,  0.5f,    "%.5f");
		}

		ImGui::Separator();
		if (ImGui::CollapsingHeader("カメラ情報 (読み取り専用)")) {

			ImGui::BeginDisabled();
			ImGui::DragFloat("Near Clip",  &debugData_.nearClip,  0.0f);
			ImGui::DragFloat("Far Clip",   &debugData_.farClip,   0.0f);
			float renderW = debugData_.renderSize.x;
			float renderH = debugData_.renderSize.y;
			ImGui::DragFloat("Width",  &renderW, 0.0f);
			ImGui::DragFloat("Height", &renderH, 0.0f);
			ImGui::EndDisabled();
		}

		ImGui::PopID();
		ImGui::End();
	}*/

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