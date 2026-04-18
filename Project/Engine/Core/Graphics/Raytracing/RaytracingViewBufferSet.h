#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Common/ViewConstantBuffer.h>
#include <Engine/Core/Graphics/GPUBuffer/RenderBufferRegistry.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/MathLib/Matrix4x4.h>
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector3.h>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	RaytracingViewBufferSet structures
	//============================================================================

	// レイトレーシングの描画に必要なビュー関連の定数バッファの内容
	struct RaytracingViewConstantsGPU{

		// ビュー関連の行列
		Matrix4x4 view = Matrix4x4::Identity();
		Matrix4x4 projection = Matrix4x4::Identity();
		Matrix4x4 inverseView = Matrix4x4::Identity();
		Matrix4x4 inverseProjection = Matrix4x4::Identity();
		Matrix4x4 inverseViewProjection = Matrix4x4::Identity();

		// カメラのワールド空間位置
		Vector3 cameraPos = Vector3::AnyInit(0.0f);

		// レイトレーシングのパラメータ
		float maxReflectionDistance = 600.0f;
		Vector2 renderSize = Vector2::AnyInit(1.0f);
		Vector2 invRenderSize = Vector2::AnyInit(1.0f);
		float shadowNormalBias = 0.01f;
		float reflectionIntensity = 0.65f;
		float nearClip = 0.01f;
		float farClip = 4000.0f;

		// // 追加: 反射の安定化用
		float reflectionNormalBias = 0.0005f;
		float reflectionViewBias = 0.0005f;
		float reflectionMinHitDistance = 0.0010f;
		float reflectionThicknessBase = 0.04f;

		float reflectionThicknessScale = 0.015f;
		float skyIntensity = 1.0f;
		float fresnelMin = 0.08f;
		float _pad0 = 0.0f;
	};

	//============================================================================
	//	RaytracingViewBufferSet class
	//	レイトレーシングの描画に必要なビュー関連のバッファを管理するクラス
	//============================================================================
	class RaytracingViewBufferSet {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RaytracingViewBufferSet() = default;
		~RaytracingViewBufferSet() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// データ転送
		void Upload(const ResolvedRenderView& view);

		// 描画バッファレジストリに自身のバッファを登録する
		void RegisterTo(RenderBufferRegistry& registry) const;

		// 未初期化状態にして解放する
		void Release() { initialized_ = false; }

		//--------- accessor -----------------------------------------------------

		bool IsInitialized() const { return initialized_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// レイトレーシングのビュー関連の定数バッファ
		ViewConstantBuffer<RaytracingViewConstantsGPU> params_{ "RaytracingViewConstants" };

		RaytracingViewConstantsGPU debugData_{};

		// 初期化されているかどうか
		bool initialized_ = false;
	};
} // Engine