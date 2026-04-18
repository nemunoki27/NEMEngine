#include "ViewLightCullingBufferSet.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>

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
	totalIndexCount_ = 0;

	initialized_ = false;
}

void Engine::ViewLightCullingBufferSet::Upload(
	const ResolvedRenderView& view, const PerViewLightSet& lightSet) {

	// ビューサイズからタイル数を計算
	const uint32_t viewWidth = (std::max)(view.width, 1u);
	const uint32_t viewHeight = (std::max)(view.height, 1u);
	tileCountX_ = (std::max)(DxUtils::RoundUp(viewWidth, kTileSizeX), 1u);
	tileCountY_ = (std::max)(DxUtils::RoundUp(viewHeight, kTileSizeY), 1u);
	totalTileCount_ = tileCountX_ * tileCountY_;
	totalIndexCount_ = totalTileCount_ * kMaxLocalLightsPerTile;

	// UAVバッファの必要容量を確保
	tileLightGrid_.EnsureCapacity(totalTileCount_);
	tileLightIndexList_.EnsureCapacity(totalIndexCount_);

	// パラメータを設定してGPUへ転送
	LightCullingParamsGPU params{};
	params.screenWidth = viewWidth;
	params.screenHeight = viewHeight;
	params.tileSizeX = kTileSizeX;
	params.tileSizeY = kTileSizeY;
	params.tileCountX = tileCountX_;
	params.tileCountY = tileCountY_;
	params.totalTileCount = totalTileCount_;
	params.maxLocalLightsPerTile = kMaxLocalLightsPerTile;
	params.pointLightCount = lightSet.GetPointCount();
	params.spotLightCount = lightSet.GetSpotCount();
	params.localLightCount = lightSet.GetLocalLightCount();
	// ビュー/プロジェクション行列を設定
	if (lightSet.camera && lightSet.camera->valid) {

		params.viewMatrix = lightSet.camera->matrices.viewMatrix;
		params.projectionMatrix = lightSet.camera->matrices.projectionMatrix;
		params.inverseProjectionMatrix = lightSet.camera->matrices.inverseProjectionMatrix;
		params.nearClip = lightSet.camera->nearClip;
		params.farClip = lightSet.camera->farClip;
	}
	// GPUへ転送
	params_.Upload(params);
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
		.elementCount = totalTileCount_,.stride = sizeof(TileLightGridEntryGPU) });
	registry.Register({ .alias = "gTileLightGrid",.resource = tileLightGrid_.GetResource(),.gpuAddress = tileLightGrid_.GetGPUAddress(),
		.srvGPUHandle = tileLightGrid_.GetSRVGPUHandle(),.uavGPUHandle = tileLightGrid_.GetUAVGPUHandle(),
		.elementCount = totalTileCount_,.stride = sizeof(TileLightGridEntryGPU) });

	// タイルライトインデックスリスト: SRV/UAV
	registry.Register({ .alias = "TileLightIndexList",.resource = tileLightIndexList_.GetResource(),
		.gpuAddress = tileLightIndexList_.GetGPUAddress(),.srvGPUHandle = tileLightIndexList_.GetSRVGPUHandle(),
		.uavGPUHandle = tileLightIndexList_.GetUAVGPUHandle(),.elementCount = totalIndexCount_,.stride = sizeof(uint32_t) });
	registry.Register({ .alias = "gTileLightIndexList",.resource = tileLightIndexList_.GetResource(),
		.gpuAddress = tileLightIndexList_.GetGPUAddress(),.srvGPUHandle = tileLightIndexList_.GetSRVGPUHandle(),
		.uavGPUHandle = tileLightIndexList_.GetUAVGPUHandle(),.elementCount = totalIndexCount_,.stride = sizeof(uint32_t) });
}