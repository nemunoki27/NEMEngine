#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackSettings.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <array>
#include <cstdint>

namespace Engine {

	// front
	class GraphicsCore;
	class MultiRenderTarget;
	class PipelineStateCache;
	class RenderAssetLibrary;
	class ECSWorld;
	struct SceneExecutionContext;

	//============================================================================
	//	ColorPipelineProcessor class
	// HDRシーンカラーの露出決定とToneMapをビュー単位で管理する
	//============================================================================
	class ColorPipelineProcessor {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ColorPipelineProcessor();
		~ColorPipelineProcessor() = default;

		// フレーム単位の一時CBV領域を再利用可能にする
		void BeginFrame();
		// GPUバッファとDescriptorを解放する
		void Release();

		// sourceを露出とカラー設定に従ってToneMapしdestへ描画する
		bool ToneMap(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
			MultiRenderTarget* source, MultiRenderTarget* dest,
			RenderAssetLibrary& assetLibrary, PipelineStateCache& pipelineCache,
			const ColorPipelineSettings& settings, bool updateExposure);
		// ToneMap済みビューをDisplay色空間へ変換してバックバッファへ描画する
		bool PresentToBackBuffer(GraphicsCore& graphicsCore,
			MultiRenderTarget* source, RenderAssetLibrary& assetLibrary,
			PipelineStateCache& pipelineCache);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 自動露出CSとToneMap PSで共有する定数
		struct ColorPipelineConstants {

			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t exposureMode = 0;
			uint32_t resetExposure = 0;

			float manualEV100 = 0.0f;
			float exposureCompensation = 0.0f;
			float minEV100 = -10.0f;
			float maxEV100 = 20.0f;

			float histogramLowPercent = 0.8f;
			float histogramHighPercent = 0.95f;
			float speedUp = 3.0f;
			float speedDown = 1.0f;

			float deltaTime = 0.0f;
			float usePreExposure = 1.0f;
			float filmicSlope = 1.0f;
			float filmicToe = 0.0f;

			float filmicShoulder = 0.0f;
			float filmicBlackClip = 0.0f;
			float filmicWhiteClip = 0.0f;
			float _pad0 = 0.0f;

			Color4 colorFilter = Color4::White();

			Vector3 whiteBalance = Vector3::AnyInit(1.0f);
			float _pad1 = 0.0f;

			Vector3 saturation = Vector3::AnyInit(1.0f);
			float _pad2 = 0.0f;

			Vector3 contrast = Vector3::AnyInit(1.0f);
			float _pad3 = 0.0f;

			Vector3 gamma = Vector3::AnyInit(1.0f);
			float _pad4 = 0.0f;

			Vector3 gain = Vector3::AnyInit(1.0f);
			float _pad5 = 0.0f;

			Vector3 offset = Vector3::AnyInit(0.0f);
			float _pad6 = 0.0f;

			uint32_t outputMode = 0;
			float paperWhiteNits = 200.0f;
			float maxLuminanceNits = 1000.0f;
			float _pad7 = 0.0f;
		};

		struct OutputTransformConstants {

			uint32_t outputMode = 0;
			float paperWhiteNits = 200.0f;
			float maxLuminanceNits = 1000.0f;
			float _pad0 = 0.0f;
		};

		struct ViewExposureState {

			// x=current exposure、y=previous exposure、z=target EV100、w=current EV100
			StructuredRWBuffer<Vector4> exposureBuffer{ "gExposureState" };
			const ECSWorld* world = nullptr;
			uint64_t lastUpdatedFrame = 0;
			bool initialized = false;
			bool bufferInitialized = false;
		};

		//--------- variables ----------------------------------------------------

		std::array<ViewExposureState, 2> viewStates_{};
		PostProcessConstantBufferAllocator constantBufferAllocator_{};
		bool outputTransformLogged_ = false;

		PipelineBindingCache exposureBindingCache_{};
		PipelineBindingCache toneMapBindingCache_{};
		PipelineBindingCache outputTransformBindingCache_{};

		PipelineBindingCache::SlotID exposureConstantsSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID exposureSourceSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID exposureOutputSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID toneMapConstantsSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID toneMapSourceSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID toneMapExposureSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outputTransformConstantsSlot_ =
			PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outputTransformSourceSlot_ =
			PipelineBindingCache::kInvalidSlot;

		//--------- functions ----------------------------------------------------

		ViewExposureState& GetViewState(RenderViewKind kind);
		ColorPipelineConstants BuildConstants(GraphicsCore& graphicsCore,
			const SceneExecutionContext& context,
			const MultiRenderTarget& source, const ColorPipelineSettings& settings,
			bool resetExposure) const;
		bool UpdateExposure(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
			MultiRenderTarget& source, RenderAssetLibrary& assetLibrary,
			PipelineStateCache& pipelineCache, ViewExposureState& state,
			const ColorPipelineConstants& constants,
			D3D12_GPU_VIRTUAL_ADDRESS constantsAddress);
		bool DrawToneMap(GraphicsCore& graphicsCore, MultiRenderTarget& source,
			MultiRenderTarget& dest, RenderAssetLibrary& assetLibrary,
			PipelineStateCache& pipelineCache, ViewExposureState& state,
			D3D12_GPU_VIRTUAL_ADDRESS constantsAddress);
		static Vector3 CalculateWhiteBalance(float temperature, float tint);
	};
} // Engine
