#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Foundation/Math/Color.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <memory>
#include <array>
#include <vector>

namespace Engine {

	// front
	class GraphicsCore;
	class DepthTexture2D;

	//============================================================================
	//	SceneGridRenderer class
	//	Sceneのグリッドを描画する
	//============================================================================
	class SceneGridRenderer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SceneGridRenderer();
		~SceneGridRenderer();

		void Init(GraphicsCore& graphicsCore);

		// フレーム開始処理
		void BeginFrame();

		// 正のfixedMinorStepで間隔を固定する
		// 色と減衰は通常描画と共通
		// 指定した深度でMeshの後ろの線を隠す
		void Render(GraphicsCore& graphicsCore, const ResolvedCameraView& camera, MultiRenderTarget& surface,
			float fixedMinorStep = 0.0f, DepthTexture2D* occlusionDepth = nullptr);

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct GridPassConstants {

			Matrix4x4 inverseViewProjectionMatrix = Matrix4x4::Identity();
			Matrix4x4 viewProjectionMatrix = Matrix4x4::Identity();

			// xyz: Camera位置、w: グリッド高さ
			Vector4 cameraPositionAndPlaneY = Vector4(0.0f, 0.0f, 0.0f, 0.0f);

			// xy: 描画幅と高さ
			Vector4 viewportSize = Vector4(1.0f, 1.0f, 0.0f, 0.0f);

			// xyz: 下側の線間隔、w: 表示半径
			Vector4 stepData0 = Vector4(1.0f, 10.0f, 100.0f, 1000.0f);

			// xyz: 上側の線間隔、w: 補間率
			Vector4 stepData1 = Vector4(2.0f, 20.0f, 200.0f, 0.0f);

			// x: 線幅の減衰指数
			// y: 最小半幅
			// z: 地平線の減衰開始
			// w: 地平線の減衰終了
			Vector4 thicknessFadeAndHorizon = Vector4(0.65f, 0.28f, 0.015f, 0.08f);

			Color4 minorColor = Color4(1.0f, 1.0f, 1.0f, 0.12f);
			// xy: 半幅と遠方の線幅率、zw: 減衰距離
			Vector4 minorParams0 = Vector4(1.0f, 0.15f, 0.0f, 300.0f);
			// x: 減衰指数
			Vector4 minorParams1 = Vector4(1.50f, 0.0f, 0.0f, 0.0f);

			Color4 majorColor = Color4(1.0f, 1.0f, 1.0f, 0.20f);
			Vector4 majorParams0 = Vector4(1.35f, 0.40f, 20.0f, 700.0f);
			Vector4 majorParams1 = Vector4(1.20f, 0.0f, 0.0f, 0.0f);

			Color4 coarseColor = Color4(1.0f, 1.0f, 1.0f, 0.24f);
			Vector4 coarseParams0 = Vector4(1.70f, 0.70f, 50.0f, 1000.0f);
			Vector4 coarseParams1 = Vector4(1.10f, 0.0f, 0.0f, 0.0f);

			Color4 axisXColor = Color4(0.95f, 0.25f, 0.25f, 0.95f);
			Color4 axisZColor = Color4(0.30f, 0.50f, 1.00f, 0.95f);
			// x: 軸線の半幅、w: 表示距離
			Vector4 axisParams = Vector4(2.4f, 0.0f, 0.0f, 1000.0f);
		};

		//--------- functions ----------------------------------------------------

		// 表示範囲からGrid描画定数を作成する
		GridPassConstants BuildPassConstants(
			const ResolvedCameraView& camera, uint32_t width, uint32_t height, float fixedMinorStep) const;
		// 描画回数に対応する定数Bufferを確保する
		DxConstBuffer<GridPassConstants>& AllocatePassBuffer(GraphicsCore& graphicsCore);

		//--------- variables ----------------------------------------------------

		std::unique_ptr<PipelineState> pipeline_{};
		// frameと描画ごとに定数Bufferを保持する
		std::array<std::vector<std::unique_ptr<DxConstBuffer<GridPassConstants>>>, kGraphicsFrameContextCount> passBuffers_{};
		std::array<uint32_t, kGraphicsFrameContextCount> passBufferIndices_{};
		uint64_t passFrameSerial_ = UINT64_MAX;

		// グリッド定数のBinding slot
		PipelineBindingCache gridBindCache_{};
		PipelineBindingCache::SlotID gridCBVSlot_ = PipelineBindingCache::kInvalidSlot;

		bool initialized_ = false;

		//------------------------------------------------------------------------
		// グリッド平面と地平線
		//------------------------------------------------------------------------

		float gridPlaneY_ = 0.0f;
		float gridHorizonFadeStart_ = 0.0001f;
		float gridHorizonFadeEnd_ = 0.6042f;

		//------------------------------------------------------------------------
		// 表示範囲と視線距離
		//------------------------------------------------------------------------

		int gridVisiblePolygonSamplesPerEdge_ = 80;
		float gridMaxGroundRayDistance_ = 20000.0f;

		//------------------------------------------------------------------------
		// 補助線ステップの自動調整
		//------------------------------------------------------------------------

		float gridMinorBaseHeightDivisor_ = 20.0f;
		float gridMinorBaseMinStep_ = 0.020f;
		float gridMinorTargetPixelMin_ = 64.0f;
		float gridMinorTargetPixelMax_ = 256.0f;

		//------------------------------------------------------------------------
		// 表示半径
		//------------------------------------------------------------------------

		float gridRadiusCoarseStepRate_ = 20.0f;
		float gridRadiusMin_ = 768.0f;
		float gridRadiusMax_ = 8000.0f;

		//------------------------------------------------------------------------
		// 線幅の減衰
		//------------------------------------------------------------------------

		float gridThicknessFadePower_ = 8.0f;
		float gridMinHalfThickness_ = 0.010f;

		//------------------------------------------------------------------------
		// 軸線
		//------------------------------------------------------------------------

		Color4 gridAxisXLineColor_ = Color4::FromHex(0xFF0009FF);
		Color4 gridAxisZLineColor_ = Color4::FromHex(0x00FF03FF);
		float gridAxisLineThickness_ = 0.4f;

		//------------------------------------------------------------------------
		// 粗い補助線
		//------------------------------------------------------------------------

		float gridCoarseBaseAlpha_ = 0.220f;
		float gridCoarseLineThickness_ = 1.700f;
		float gridCoarseFarThicknessRate_ = 0.010f;
		float gridCoarseFadeStartRate_ = 0.250f;
		float gridCoarseFadeEndRate_ = 1.000f;
		float gridCoarseFadePower_ = 2.900f;

		//------------------------------------------------------------------------
		// 主補助線
		//------------------------------------------------------------------------

		float gridMajorBaseAlpha_ = 0.120f;
		float gridMajorLineThickness_ = 0.010f;
		float gridMajorFarThicknessRate_ = 0.010f;
		float gridMajorFadeStartRate_ = 0.000f;
		float gridMajorFadeEndRate_ = 4.311f;
		float gridMajorFadePower_ = 3.200f;

		//------------------------------------------------------------------------
		// 細い補助線
		//------------------------------------------------------------------------

		float gridMinorBaseAlpha_ = 0.050f;
		float gridMinorLineThickness_ = 1.000f;
		float gridMinorFarThicknessRate_ = 0.150f;
		float gridMinorFadeStartRate_ = 0.000f;
		float gridMinorFadeEndRate_ = 0.980f;
		float gridMinorFadePower_ = 0.790f;
	};
} // Engine
