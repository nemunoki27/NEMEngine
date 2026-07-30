#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxReadbackBuffer.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	// front
	class GraphicsCore;
	class ECSWorld;

	//============================================================================
	//	MeshSubMeshPicker structures
	//============================================================================
	struct MeshSubMeshPickOutcome {

		// CommitScenePickを行うべきか
		bool committed = false;
		// エンティティにヒットしたか
		bool hit = false;

		Entity entity = Entity::Null();
		uint32_t subMeshIndex = 0;
		UUID subMeshStableID{};
	};

	//============================================================================
	//	MeshSubMeshPicker class
	//	1x1整数RTでメッシュのサブメッシュを取得するクラス
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
		// ラスター描画済みの1x1整数RTをreadbackへコピーする
		void ExecuteReadback(GraphicsCore& graphicsCore);
		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

		bool HasPendingReadback() const { return pendingReadback_; }
		MultiRenderTarget* GetRenderTarget() {
			return renderTarget_.IsValid() ? &renderTarget_ : nullptr;
		}
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
		// テクスチャコピーのRowPitchは256byte境界に合わせる
		struct PickReadbackRow {

			PickResult result{};
			uint32_t padding[60]{};
		};
		static_assert(sizeof(PickReadbackRow) == D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);

		//--------- variables ----------------------------------------------------

		MultiRenderTarget renderTarget_{};
		DxReadbackBuffer<PickReadbackRow> readbackBuffer_{};

		bool initialized_ = false;
		bool pendingReadback_ = false;
		uint32_t pendingFrameIndex_ = 0;
	};
} // Engine
