#pragma once

//============================================================================
//	include
//============================================================================
#include "GlobalIlluminationSettings.h"
#include "GlobalIlluminationGeometry.h"
#include <Engine/Core/Rendering/Raytracing/RaytracingSceneBuilder.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingPipelineState.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <array>
#include <functional>
#include <string_view>

namespace Engine {

	//============================================================================
	//	GlobalIlluminationView class
	//	Camera別のProbe更新とGPU資源を所有する
	//============================================================================
	class GlobalIlluminationView {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		GlobalIlluminationView() = default;
		~GlobalIlluminationView();

		// 照明計算前に間接光を更新する
		void Update(GraphicsCore& graphicsCore, SceneExecutionContext& context, RenderAssetLibrary& assetLibrary,
			MaterialResolver& materialResolver, MeshRenderBackend* meshBackend,
			PrimitiveGeometryManager* primitiveGeometryManager, const RenderSceneBatch& batch, uint32_t updateCount);
		// 既存の形状Cacheを借用する
		void ShareGeometryCache(const RaytracingSceneBuilder& source);
		// 確定したProbeをLightingへ設定する
		void BindLighting(GraphicsCore& graphicsCore, const PipelineState& pipeline) const;
		// 反射の命中面にも同じ間接光を渡す
		void BindReflection(GraphicsCore& graphicsCore, const RaytracingPipelineState& pipeline) const;
		// 無効化したCameraの資源を回収する
		void Release();

		//--------- accessor -----------------------------------------------------

		bool IsReady() const { return ready_; }
		uint32_t GetUpdatedProbeCount() const { return constants_.updateCount; }
		uint64_t GetMemoryBytes() const;
		// 確定済みProbeを検証用に参照する
		const RenderTexture2D* FindDiagnosticTexture(std::string_view name) const;
		// 待機せず取得済みのGPU時間から投入量を求める
		uint32_t GetUpdateCount(float budgetMilliseconds, const ResolvedRenderView& view, uint32_t quality) const;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct ProbeConstants {

			std::array<Vector4, 3> gridOrigins{};
			uint32_t gridCount = 8;
			uint32_t levelCount = 3;
			uint32_t startProbe = 0;
			uint32_t updateCount = 0;
			uint32_t rayCount = 128;
			uint32_t frameIndex = 0;
			uint32_t resetHistory = 1;
			uint32_t enabled = 1;
			float maxRayDistance = 80.0f;
			float hysteresis = 0.9f;
			float normalBias = 0.1f;
			float viewBias = 0.02f;
			uint32_t debugMode = 0;
			std::array<uint32_t, 3> padding{};
		};
		static_assert(sizeof(ProbeConstants) == 112);
		struct ProbeField {

			RenderTexture2D irradiance;
			RenderTexture2D distance;
			RenderTexture2D positions;
			RenderTexture2D offsets;
		};

		struct UpdateRecord {

			uint64_t frameID = 0;
			uint32_t probeCount = 0;
		};

		//--------- variables ----------------------------------------------------

		static constexpr uint32_t kProbeCount = 8 * 8 * 8 * 3;
		std::array<ProbeField, 2> fields_{};
		RenderTexture2D rayResults_;
		RaytracingSceneBuilder sceneBuilder_;
		GlobalIlluminationMaterials materials_;
		GlobalIlluminationGeometry geometry_;
		const RaytracingSceneBuilder* geometryCacheSource_ = nullptr;
		uint32_t pipelineGraphCount_ = UINT32_MAX;
		std::shared_ptr<const uint64_t> graphRevision_;
		std::unique_ptr<RaytracingPipelineState> tracePipeline_;
		std::unique_ptr<PipelineState> blendPipeline_;
		FrameConstantBufferAllocator constantAllocator_;
		ProbeConstants constants_{};
		D3D12_GPU_VIRTUAL_ADDRESS constantsAddress_ = 0;
		uint32_t publishedField_ = 0;
		uint32_t nextProbe_ = 0;
		std::array<Vector3, kProbeCount> publishedPositions_{};
		std::array<bool, kProbeCount> publishedPositionsValid_{};
		uint32_t cullingMask_ = 0;
		GlobalIlluminationSettings settings_{};
		bool initialized_ = false;
		bool ready_ = false;
		uint64_t frameSerial_ = UINT64_MAX;
		uint64_t materialGeneration_ = 0;
		std::array<UpdateRecord, 8> updateRecords_{};

		//--------- functions ----------------------------------------------------

		// PipelineとProbeの転送先を生成する
		bool EnsureResources(GraphicsCore& graphicsCore);
		// 使用中のGraphを含むDXR Pipelineを確定
		bool EnsureTracePipeline(GraphicsCore& graphicsCore);
		// 未更新のProbeを含めて履歴を初期化する
		void ClearFields(GraphicsCore& graphicsCore);
		// 前回の確定結果を次の書込先へ複製する
		void CopyField(GraphicsCore& graphicsCore, uint32_t destination);
		// Reflectionに従って共通BufferとProbeを接続する
		bool BindCompute(GraphicsCore& graphicsCore, const SceneExecutionContext& context,
			const ShaderReflectionInfo& reflection,
			const std::function<const RootBindingLocation*(std::string_view, ShaderBindingKind)>& findBinding,
			uint32_t destination);
		// 環状格子の保存位置を求める
		Vector3 GetNominalProbePosition(uint32_t index) const;
		// Camera位置から格子の原点を更新する
		void UpdateGrid(const Vector3& cameraPosition, const GlobalIlluminationSettings& settings, uint32_t updateCount);
	};
} // Engine
