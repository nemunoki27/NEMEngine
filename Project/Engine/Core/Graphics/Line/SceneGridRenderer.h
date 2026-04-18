#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Graphics/GPUBuffer/DxConstBuffer.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/Graphics/Render/Texture/MultiRenderTarget.h>
#include <Engine/MathLib/Color.h>
#include <Engine/MathLib/Vector4.h>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	SceneGridRenderer class
	//	analytic grid shader によるシーングリッド描画
	//============================================================================
	class SceneGridRenderer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void Init(GraphicsCore& graphicsCore);
		void Render(GraphicsCore& graphicsCore, const ResolvedCameraView& camera, MultiRenderTarget& surface);
		void Edit();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct GridPassConstants {

			Matrix4x4 inverseViewProjectionMatrix = Matrix4x4::Identity();
			Matrix4x4 viewProjectionMatrix = Matrix4x4::Identity();

			// xyz : camera position, w : grid plane Y
			Vector4 cameraPositionAndPlaneY = Vector4(0.0f, 0.0f, 0.0f, 0.0f);

			// x : width, y : height
			Vector4 viewportSize = Vector4(1.0f, 1.0f, 0.0f, 0.0f);

			// x : minor step, y : major step, z : coarse step, w : visible radius
			// x : minor step0, y : major step0, z : coarse step0, w : visible radius
			Vector4 stepData0 = Vector4(1.0f, 10.0f, 100.0f, 1000.0f);

			// x : minor step1, y : major step1, z : coarse step1, w : step cross-fade blend
			Vector4 stepData1 = Vector4(2.0f, 20.0f, 200.0f, 0.0f);

			// x : thickness fade power
			// y : minimum half thickness
			// z : horizon fade start (abs(rayDir.y))
			// w : horizon fade end   (abs(rayDir.y))
			Vector4 thicknessFadeAndHorizon = Vector4(0.65f, 0.28f, 0.015f, 0.08f);

			Color minorColor = Color(1.0f, 1.0f, 1.0f, 0.12f);
			// x : half thickness, y : far thickness rate, z : fade start distance, w : fade end distance
			Vector4 minorParams0 = Vector4(1.0f, 0.15f, 0.0f, 300.0f);
			// x : fade power
			Vector4 minorParams1 = Vector4(1.50f, 0.0f, 0.0f, 0.0f);

			Color majorColor = Color(1.0f, 1.0f, 1.0f, 0.20f);
			Vector4 majorParams0 = Vector4(1.35f, 0.40f, 20.0f, 700.0f);
			Vector4 majorParams1 = Vector4(1.20f, 0.0f, 0.0f, 0.0f);

			Color coarseColor = Color(1.0f, 1.0f, 1.0f, 0.24f);
			Vector4 coarseParams0 = Vector4(1.70f, 0.70f, 50.0f, 1000.0f);
			Vector4 coarseParams1 = Vector4(1.10f, 0.0f, 0.0f, 0.0f);

			Color axisXColor = Color(0.95f, 0.25f, 0.25f, 0.95f);
			Color axisZColor = Color(0.30f, 0.50f, 1.00f, 0.95f);
			// x : half thickness, w : axis visible distance
			Vector4 axisParams = Vector4(2.4f, 0.0f, 0.0f, 1000.0f);
		};

		//--------- variables ----------------------------------------------------

		PipelineState pipeline_{};
		DxConstBuffer<GridPassConstants> passBuffer_{};

		bool initialized_ = false;

		//------------------------------------------------------------------------
		// grid plane / horizon
		//------------------------------------------------------------------------

		float gridPlaneY_ = 0.0f;
		float gridHorizonFadeStart_ = 0.0001f;
		float gridHorizonFadeEnd_ = 0.6042f;

		//------------------------------------------------------------------------
		// visible polygon / ray hit
		//------------------------------------------------------------------------

		int gridVisiblePolygonSamplesPerEdge_ = 80;
		float gridMaxGroundRayDistance_ = 20000.0f;

		//------------------------------------------------------------------------
		// minor step auto fitting
		//------------------------------------------------------------------------

		float gridMinorBaseHeightDivisor_ = 20.0f;
		float gridMinorBaseMinStep_ = 0.020f;
		float gridMinorTargetPixelMin_ = 64.0f;
		float gridMinorTargetPixelMax_ = 256.0f;
		int gridMinorStepAdjustMaxIteration_ = 18;

		//------------------------------------------------------------------------
		// visible radius
		//------------------------------------------------------------------------

		float gridRadiusCoarseStepRate_ = 20.0f;
		float gridRadiusMin_ = 768.0f;
		float gridRadiusMax_ = 8000.0f;

		//------------------------------------------------------------------------
		// thickness fade
		//------------------------------------------------------------------------

		float gridThicknessFadePower_ = 8.0f;
		float gridMinHalfThickness_ = 0.010f;

		//------------------------------------------------------------------------
		// axis
		//------------------------------------------------------------------------

		Color gridAxisXLineColor_ = Color::FromHex(0xFF0009FF);
		Color gridAxisZLineColor_ = Color::FromHex(0x00FF03FF);
		float gridAxisLineThickness_ = 0.4f;

		//------------------------------------------------------------------------
		// coarse layer
		//------------------------------------------------------------------------

		float gridCoarseBaseAlpha_ = 0.220f;
		float gridCoarseLineThickness_ = 1.700f;
		float gridCoarseFarThicknessRate_ = 0.010f;
		float gridCoarseFadeStartRate_ = 0.250f;
		float gridCoarseFadeEndRate_ = 1.000f;
		float gridCoarseFadePower_ = 2.900f;

		//------------------------------------------------------------------------
		// major layer
		//------------------------------------------------------------------------

		float gridMajorBaseAlpha_ = 0.120f;
		float gridMajorLineThickness_ = 0.010f;
		float gridMajorFarThicknessRate_ = 0.010f;
		float gridMajorFadeStartRate_ = 0.000f;
		float gridMajorFadeEndRate_ = 4.311f;
		float gridMajorFadePower_ = 3.200f;

		//------------------------------------------------------------------------
		// minor layer
		//------------------------------------------------------------------------

		float gridMinorBaseAlpha_ = 0.050f;
		float gridMinorLineThickness_ = 1.000f;
		float gridMinorFarThicknessRate_ = 0.150f;
		float gridMinorFadeStartRate_ = 0.000f;
		float gridMinorFadeEndRate_ = 0.980f;
		float gridMinorFadePower_ = 0.790f;

		//--------- functions ----------------------------------------------------

		GridPassConstants BuildPassConstants(const ResolvedCameraView& camera, uint32_t width, uint32_t height) const;
	};
} // Engine