#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/BuiltinRenderBackendBase.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveBatchResources.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/FrameBatchResourcePool.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>

// c++
#include <array>

namespace Engine {

	//============================================================================
	//	PrimitiveRenderBackend class
	//	プロシージャル形状を共有ジオメトリでインスタンシング描画する、不透明はGBuffer半透明はフォワード
	//============================================================================
	class PrimitiveRenderBackend :
		public BuiltinRenderBackendBase {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PrimitiveRenderBackend() {

			meshConstantsCBVSlot_ = perDrawBindCache_.AddSlot("PrimitiveMeshConstants", ShaderBindingKind::CBV);
			verticesSRVSlot_ = perDrawBindCache_.AddSlot("gVertices", ShaderBindingKind::SRV);
			instancesSRVSlot_ = perDrawBindCache_.AddSlot("gInstances", ShaderBindingKind::SRV);
			indicesSRVSlot_ = perDrawBindCache_.AddSlot("gIndices", ShaderBindingKind::SRV);
			// 選択アウトラインのマスク描画で使うStyle ID
			outlineMaskCBVSlot_ = perDrawBindCache_.AddSlotByRegister(ShaderBindingKind::CBV,
				kScreenSpaceOutlineMaskCBVRegister, kScreenSpaceOutlineMaskCBVSpace);
		}
		~PrimitiveRenderBackend() override;

		void BeginFrame(GraphicsCore& graphicsCore) override;

		void DrawBatch(const RenderDrawContext& context, std::span<const RenderItem* const> items) override;

		//--------- accessor -----------------------------------------------------

		uint32_t GetID() const override { return RenderBackendID::Primitive; }
		// レイトレ側から共有ジオメトリを参照するため公開する
		PrimitiveGeometryManager& GetGeometryManager() { return geometryManager_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 形状ハッシュ単位で共有するジオメトリ
		PrimitiveGeometryManager geometryManager_{};
		bool geometryManagerInitialized_ = false;

		FrameBatchResourcePool<PrimitiveBatchResources> resourcePool_;
		std::array<Matrix4x4, 2> previousViewProjections_ = {
			Matrix4x4::Identity(), Matrix4x4::Identity()
		};
		std::array<Matrix4x4, 2> framePreviousViewProjections_ = {
			Matrix4x4::Identity(), Matrix4x4::Identity()
		};
		std::array<uint64_t, 2> viewFrameSerials_ = { 0, 0 };
		std::array<bool, 2> previousViewValid_ = { false, false };

		PipelineBindingCache::SlotID meshConstantsCBVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID verticesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID instancesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID indicesSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outlineMaskCBVSlot_ = PipelineBindingCache::kInvalidSlot;

		//--------- functions ----------------------------------------------------

		// バッチのインスタンスデータを集める
		void CollectInstances(const RenderDrawContext& context, std::span<const RenderItem* const> items,
			std::vector<PrimitiveInstanceData>& outInstances) const;
	};
} // Engine
