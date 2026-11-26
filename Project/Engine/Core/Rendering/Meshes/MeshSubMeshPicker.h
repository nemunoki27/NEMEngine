#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/GraphicsFrameResourceArray.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxReadbackBuffer.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <vector>

namespace Engine {

	// front
	class GraphicsCore;
	class ECSWorld;

	//============================================================================
	//	MeshSubMeshPicker structures
	//============================================================================
	struct MeshSubMeshPickOutcome {

		// GPU結果が解決済みか
		bool resolved = false;
		// エンティティにヒットしたか
		bool hit = false;
		// クリック要求との対応付けに使うID
		uint64_t requestID = 0;

		Entity entity = Entity::Null();
		uint32_t subMeshIndex = 0;
		UUID subMeshStableID{};
		bool region = false;
		std::vector<Entity> entities;
	};

	//============================================================================
	//	MeshSubMeshPicker class
	//	整数RTから選択範囲のオブジェクトとクリック位置のサブメッシュを取得する
	//============================================================================
	class MeshSubMeshPicker {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MeshSubMeshPicker() = default;
		~MeshSubMeshPicker() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);
		// 前フレームの結果を選択情報へ変換する
		MeshSubMeshPickOutcome ConsumePendingResult(
			GraphicsCore& graphicsCore, ECSWorld* world);
		// ピッキングRTからreadbackへのコピーを予約する
		bool ExecuteReadback(GraphicsCore& graphicsCore,
			uint64_t requestID, MultiRenderTarget& target, bool region = false);
		// 矩形選択のフレームだけID描画先を確保する
		MultiRenderTarget* GetRegionTarget(GraphicsCore& graphicsCore, uint32_t width, uint32_t height);
		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		MultiRenderTarget* GetRenderTarget() { return renderTarget_.IsValid() ? &renderTarget_ : nullptr; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct PickResult {

			uint32_t entityIndex = UINT32_MAX;
			uint32_t entityGeneration = UINT32_MAX;
			uint32_t subMeshIndex = UINT32_MAX;
			uint32_t valid = 0;
		};
		struct ReadbackSlot {

			DxReadbackBuffer<PickResult> clickBuffer;
			std::unique_ptr<DxReadbackBuffer<PickResult>> regionBuffer;
			std::unique_ptr<MultiRenderTarget> regionTarget;
			uint32_t width = 0;
			uint32_t height = 0;
			uint32_t rowLength = 0;
			bool region = false;
			uint64_t requestID = 0;
			uint64_t fenceValue = 0;
			bool pending = false;
		};

		//--------- variables ----------------------------------------------------

		MultiRenderTarget renderTarget_{};
		GraphicsFrameResourceArray<ReadbackSlot> readbackSlots_{};

		bool initialized_ = false;
	};
} // Engine
