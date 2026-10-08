#include "SceneGridRenderer.h"
#include "SceneGridLayout.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Pipelines/BuiltinShaderSource.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <algorithm>
#include <vector>

//============================================================================
//	SceneGridRenderer classMethods
//============================================================================
using namespace Engine::SceneGridLayout;

Engine::SceneGridRenderer::SceneGridRenderer() {

	// 定数BufferのBinding slotを登録する
	gridCBVSlot_ = gridBindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 0, 0);
}

Engine::SceneGridRenderer::~SceneGridRenderer() {

	// 描画ごとの定数Bufferを解放する
	for (auto& frameBuffers : passBuffers_) {
		for (auto& buffer : frameBuffers) {
			buffer.reset();
		}
		frameBuffers.clear();
	}
}

void Engine::SceneGridRenderer::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
	DxShaderCompiler* compiler = graphicsCore.GetDXObject().GetDxShaderCompiler();

	GraphicsPipelineDesc desc{};
	desc.type = PipelineType::Vertex;

	desc.preRaster.file = BuiltinShaderSource::Line::AnalyticGridVS;
	desc.preRaster.entry = "main";
	desc.preRaster.profile = "vs_6_0";

	desc.pixel.file = BuiltinShaderSource::Line::AnalyticGridPS;
	desc.pixel.entry = "main";
	desc.pixel.profile = "ps_6_0";

	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;

	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = TRUE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	desc.depthStencil.StencilEnable = FALSE;

	desc.sampleDesc = {1, 0};
	desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	desc.numRenderTargets = 1;
	desc.rtvFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	bool created = (pipeline_ = PipelineStateBuilder::CreateGraphics(
						graphicsCore.GetDXObject().GetResourceRetirement(), device, compiler, desc)) != nullptr;
	Assert::Call(created, "SceneGridRendererの解析グリッドPipeline作成に失敗しました");

	for (auto& buffers : passBuffers_) {
		buffers.reserve(4);
	}

	initialized_ = created;
}

void Engine::SceneGridRenderer::BeginFrame() {

	// 同frameの再描画では先の定数領域を残す
	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	if (passFrameSerial_ == serial) {
		return;
	}
	passFrameSerial_ = serial;
	passBufferIndices_[GraphicsFrameState::GetCurrentIndex()] = 0;
}

Engine::SceneGridRenderer::GridPassConstants Engine::SceneGridRenderer::BuildPassConstants(
	const ResolvedCameraView& camera, uint32_t width, uint32_t height, float fixedMinorStep) const {

	GridPassConstants constants{};

	std::vector<GridPoint2D> polygon{};
	bool hasPolygon =
		BuildVisibleGroundPolygon(camera, gridVisiblePolygonSamplesPerEdge_, gridPlaneY_, gridMaxGroundRayDistance_, polygon);

	// 固定間隔と画面幅による間隔を切り替える
	float minorStep0;
	float minorStep1;
	float stepBlendValue;
	if (fixedMinorStep > 0.0f) {

		minorStep0 = fixedMinorStep;
		minorStep1 = fixedMinorStep;
		stepBlendValue = 0.0f;
	} else {

		const GridStepBlend stepBlend =
			DetermineMinorStepBlend(camera, width, height, polygon, gridPlaneY_, gridMaxGroundRayDistance_,
				gridMinorBaseHeightDivisor_, gridMinorBaseMinStep_, gridMinorTargetPixelMin_, gridMinorTargetPixelMax_);
		minorStep0 = stepBlend.minorStep0;
		minorStep1 = stepBlend.minorStep1;
		stepBlendValue = stepBlend.blend;
	}

	float majorStep0 = minorStep0 * 10.0f;
	float coarseStep0 = majorStep0 * 10.0f;

	float majorStep1 = minorStep1 * 10.0f;
	float coarseStep1 = majorStep1 * 10.0f;

	const float maxGridRadius =
		std::clamp((std::max)(coarseStep0, coarseStep1) * gridRadiusCoarseStepRate_, gridRadiusMin_, gridRadiusMax_);

	float visibleRadius = maxGridRadius;
	if (hasPolygon) {
		visibleRadius = 1.0f;
		for (const auto& p : polygon) {
			Vector3 point(p.x, gridPlaneY_, p.z);
			visibleRadius = (std::max)(visibleRadius, DistanceXZ(point, camera.cameraPos));
		}
		visibleRadius = (std::min)(visibleRadius, maxGridRadius);
	}

	constants.stepData0 = Vector4(minorStep0, majorStep0, coarseStep0, visibleRadius);

	constants.stepData1 = Vector4(minorStep1, majorStep1, coarseStep1, stepBlendValue);

	const auto makeFadeStartDistance = [&](float rate) { return visibleRadius * std::clamp(rate, 0.0f, 10.0f); };

	const auto makeFadeEndDistance = [&](float startRate, float endRate) {
		float startDistance = makeFadeStartDistance(startRate);
		float endDistance = visibleRadius * (std::max)(endRate, startRate + 0.001f);
		return (std::max)(endDistance, startDistance + 1.0f);
	};

	constants.inverseViewProjectionMatrix = camera.matrices.inverseProjectionMatrix * camera.matrices.inverseViewMatrix;
	constants.viewProjectionMatrix = camera.matrices.viewProjectionMatrix;

	constants.cameraPositionAndPlaneY = Vector4(camera.cameraPos.x, camera.cameraPos.y, camera.cameraPos.z, gridPlaneY_);

	constants.viewportSize = Vector4(static_cast<float>(width), static_cast<float>(height), 0.0f, 0.0f);

	constants.thicknessFadeAndHorizon =
		Vector4(gridThicknessFadePower_, gridMinHalfThickness_, gridHorizonFadeStart_, gridHorizonFadeEnd_);

	// 固定間隔の細線は不透明にする
	const float minorAlpha = (fixedMinorStep > 0.0f) ? 1.0f : gridMinorBaseAlpha_;
	constants.minorColor = Color4(1.0f, 1.0f, 1.0f, minorAlpha);
	constants.minorParams0 = Vector4(gridMinorLineThickness_, gridMinorFarThicknessRate_,
		makeFadeStartDistance(gridMinorFadeStartRate_), makeFadeEndDistance(gridMinorFadeStartRate_, gridMinorFadeEndRate_));
	constants.minorParams1 = Vector4(gridMinorFadePower_, 0.0f, 0.0f, 0.0f);

	constants.majorColor = Color4(1.0f, 1.0f, 1.0f, gridMajorBaseAlpha_);
	constants.majorParams0 = Vector4(gridMajorLineThickness_, gridMajorFarThicknessRate_,
		makeFadeStartDistance(gridMajorFadeStartRate_), makeFadeEndDistance(gridMajorFadeStartRate_, gridMajorFadeEndRate_));
	constants.majorParams1 = Vector4(gridMajorFadePower_, 0.0f, 0.0f, 0.0f);

	constants.coarseColor = Color4(1.0f, 1.0f, 1.0f, gridCoarseBaseAlpha_);
	constants.coarseParams0 = Vector4(gridCoarseLineThickness_, gridCoarseFarThicknessRate_,
		makeFadeStartDistance(gridCoarseFadeStartRate_), makeFadeEndDistance(gridCoarseFadeStartRate_, gridCoarseFadeEndRate_));
	constants.coarseParams1 = Vector4(gridCoarseFadePower_, 0.0f, 0.0f, 0.0f);

	constants.axisXColor = gridAxisXLineColor_;
	constants.axisZColor = gridAxisZLineColor_;
	constants.axisParams = Vector4(gridAxisLineThickness_, 0.0f, 0.0f, visibleRadius);

	return constants;
}

Engine::DxConstBuffer<Engine::SceneGridRenderer::GridPassConstants>& Engine::SceneGridRenderer::AllocatePassBuffer(
	GraphicsCore& graphicsCore) {

	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	auto& buffers = passBuffers_[frameIndex];
	uint32_t& bufferIndex = passBufferIndices_[frameIndex];
	if (buffers.size() <= bufferIndex) {

		// 描画ごとに別の定数領域を確保する
		auto buffer = std::make_unique<DxConstBuffer<GridPassConstants>>();
		buffer->CreateBuffer(graphicsCore.GetDXObject().GetResourceRetirement(), graphicsCore.GetDXObject().GetDevice());
		buffers.emplace_back(std::move(buffer));
	}
	return *buffers[bufferIndex++];
}

void Engine::SceneGridRenderer::Render(GraphicsCore& graphicsCore, const ResolvedCameraView& camera, MultiRenderTarget& surface,
	float fixedMinorStep, DepthTexture2D* occlusionDepth) {

	if (!initialized_) {
		return;
	}
	if (!camera.valid) {
		return;
	}
	if (surface.GetWidth() == 0 || surface.GetHeight() == 0) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	surface.TransitionForRender(*dxCommand);
	if (RenderTexture2D* color = surface.GetColorTexture(0)) {

		if (occlusionDepth) {

			// Sceneの深度でMeshの後ろの線を隠す
			occlusionDepth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			dxCommand->BindRenderTargets(
				std::optional<RenderTarget>(color->GetRenderTarget()), occlusionDepth->GetDSVCPUHandle());
		} else if (DepthTexture2D* depth = surface.GetDepthTexture()) {

			dxCommand->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()), depth->GetDSVCPUHandle());
		} else {

			dxCommand->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()), std::nullopt);
		}
		dxCommand->SetViewportAndScissor(surface.GetWidth(), surface.GetHeight());
	} else {

		return;
	}

	GridPassConstants constants = BuildPassConstants(camera, surface.GetWidth(), surface.GetHeight(), fixedMinorStep);
	DxConstBuffer<GridPassConstants>& passBuffer = AllocatePassBuffer(graphicsCore);
	passBuffer.TransferData(constants);

	commandList->SetGraphicsRootSignature(pipeline_->GetRootSignature());
	commandList->SetPipelineState(pipeline_->GetGraphicsPipeline(BlendMode::Normal));

	// パイプラインが変わった時だけスロットを再解決する
	gridBindCache_.Sync(*pipeline_);
	if (gridBindCache_.Has(gridCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(
			commandList, gridBindCache_.Get(gridCBVSlot_), passBuffer.GetResource()->GetGPUVirtualAddress());
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}
