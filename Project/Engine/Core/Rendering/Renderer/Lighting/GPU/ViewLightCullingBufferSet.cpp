#include "ViewLightCullingBufferSet.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>

void Engine::ViewLightCullingBufferSet::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor* srvDescriptor = &graphicsCore.GetSRVDescriptor();

	// バッファを初期化
	params_.Init(device);
	tileLightGrid_.Init(device, srvDescriptor);
	tileLightIndexList_.Init(device, srvDescriptor);

	// 要素数を最低限確保
	tileLightGrid_.EnsureCapacity(1);
	tileLightIndexList_.EnsureCapacity(1);

	// 初期化完了
	initialized_ = true;
}

void Engine::ViewLightCullingBufferSet::Release() {

	tileLightGrid_.Release();
	tileLightIndexList_.Release();

	tileCountX_ = 0;
	tileCountY_ = 0;
	totalTileCount_ = 0;
	totalClusterCount_ = 0;
	totalIndexCount_ = 0;
	localLightCount_ = 0;

	initialized_ = false;
}

void Engine::ViewLightCullingBufferSet::Upload(
	const ResolvedRenderView& view, const PerViewLightSet& lightSet, LightCullingMode lightCullingMode) {

	// ビューサイズからタイル数を計算
	const uint32_t viewWidth = (std::max)(view.width, 1u);
	const uint32_t viewHeight = (std::max)(view.height, 1u);
	localLightCount_ = lightSet.GetLocalLightCount();
	const bool requiresGrid =
		lightCullingMode != LightCullingMode::Disabled &&
		localLightCount_ > 0;

	const bool usesClusterGrid =
		lightCullingMode == LightCullingMode::Clustered ||
		lightCullingMode == LightCullingMode::DebugAllLightsPerCluster;
	const uint32_t maxLocalLightsPerCluster =
		lightCullingMode == LightCullingMode::DebugAllLightsPerCluster ?
		(std::max)(localLightCount_, 1u) :
		kMaxLocalLightsPerTile;

	LightCullingParamsGPU params{};
	params.screenWidth = viewWidth;
	params.screenHeight = viewHeight;
	params.tileSizeX = kTileSizeX;
	params.tileSizeY = kTileSizeY;
	params.maxLocalLightsPerTile = kMaxLocalLightsPerTile;
	params.maxLocalLightsPerCluster = maxLocalLightsPerCluster;
	params.lightCullingMode = static_cast<uint32_t>(lightCullingMode);
	params.pointLightCount = lightSet.GetPointCount();
	params.spotLightCount = lightSet.GetSpotCount();
	params.localLightCount = localLightCount_;
	params.lightCullingEnabled = requiresGrid ? 1u : 0u;
	if (lightSet.camera && lightSet.camera->valid) {

		params.viewMatrix = lightSet.camera->matrices.viewMatrix;
		params.projectionMatrix = lightSet.camera->matrices.projectionMatrix;
		params.nearClip = lightSet.camera->nearClip;
		params.farClip = lightSet.camera->farClip;
	}

	if (!requiresGrid) {

		tileCountX_ = 1;
		tileCountY_ = 1;
		totalTileCount_ = 1;
		totalClusterCount_ = 1;
		totalIndexCount_ = 1;
		params.tileCountX = tileCountX_;
		params.tileCountY = tileCountY_;
		params.totalTileCount = totalTileCount_;
		params.clusterCountZ = 1;
		params.totalClusterCount = totalClusterCount_;
		tileLightGrid_.EnsureCapacity(1);
		tileLightIndexList_.EnsureCapacity(1);
		params_.Upload(params);
		return;
	}

	tileCountX_ = (std::max)(DxUtils::RoundUp(viewWidth, kTileSizeX), 1u);
	tileCountY_ = (std::max)(DxUtils::RoundUp(viewHeight, kTileSizeY), 1u);
	totalTileCount_ = tileCountX_ * tileCountY_;
	totalClusterCount_ = totalTileCount_ * (usesClusterGrid ? kClusterCountZ : 1u);
	totalIndexCount_ = totalClusterCount_ * (usesClusterGrid ? maxLocalLightsPerCluster : kMaxLocalLightsPerTile);

	// UAVバッファの必要容量を確保
	tileLightGrid_.EnsureCapacity(totalClusterCount_);
	tileLightIndexList_.EnsureCapacity(totalIndexCount_);

	params.tileCountX = tileCountX_;
	params.tileCountY = tileCountY_;
	params.totalTileCount = totalTileCount_;
	params.clusterCountZ = usesClusterGrid ? kClusterCountZ : 1u;
	params.totalClusterCount = totalClusterCount_;
	// GPUへ転送
	params_.Upload(params);
}

void Engine::ViewLightCullingBufferSet::TransitionForComputeWrite(DxCommand& command) {

	tileLightGrid_.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	tileLightIndexList_.Transition(command, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}

void Engine::ViewLightCullingBufferSet::TransitionForShaderRead(DxCommand& command) {

	constexpr D3D12_RESOURCE_STATES kShaderRead =
		static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	tileLightGrid_.Transition(command, kShaderRead);
	tileLightIndexList_.Transition(command, kShaderRead);
}

void Engine::ViewLightCullingBufferSet::RegisterTo(RenderBufferRegistry& registry) const {

	// カリング: CBV
	registry.Register({ .alias = "LightCullingParams",.resource = nullptr,.gpuAddress = params_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(LightCullingParamsGPU) });
	registry.Register({ .alias = "gLightCullingParams",.resource = nullptr,.gpuAddress = params_.GetGPUAddress(),
		.srvGPUHandle = {},.uavGPUHandle = {},.elementCount = 1,.stride = sizeof(LightCullingParamsGPU) });

	// タイルグリッド: SRV/UAV
	registry.Register({ .alias = "TileLightGrid",.resource = tileLightGrid_.GetResource(),.gpuAddress = tileLightGrid_.GetGPUAddress(),
		.srvGPUHandle = tileLightGrid_.GetSRVGPUHandle(),.uavGPUHandle = tileLightGrid_.GetUAVGPUHandle(),
		.elementCount = totalClusterCount_,.stride = sizeof(TileLightGridEntryGPU) });
	registry.Register({ .alias = "gTileLightGrid",.resource = tileLightGrid_.GetResource(),.gpuAddress = tileLightGrid_.GetGPUAddress(),
		.srvGPUHandle = tileLightGrid_.GetSRVGPUHandle(),.uavGPUHandle = tileLightGrid_.GetUAVGPUHandle(),
		.elementCount = totalClusterCount_,.stride = sizeof(TileLightGridEntryGPU) });

	// タイルライトインデックスリスト: SRV/UAV
	registry.Register({ .alias = "TileLightIndexList",.resource = tileLightIndexList_.GetResource(),
		.gpuAddress = tileLightIndexList_.GetGPUAddress(),.srvGPUHandle = tileLightIndexList_.GetSRVGPUHandle(),
		.uavGPUHandle = tileLightIndexList_.GetUAVGPUHandle(),.elementCount = totalIndexCount_,.stride = sizeof(uint32_t) });
	registry.Register({ .alias = "gTileLightIndexList",.resource = tileLightIndexList_.GetResource(),
		.gpuAddress = tileLightIndexList_.GetGPUAddress(),.srvGPUHandle = tileLightIndexList_.GetSRVGPUHandle(),
		.uavGPUHandle = tileLightIndexList_.GetUAVGPUHandle(),.elementCount = totalIndexCount_,.stride = sizeof(uint32_t) });
}
