#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxReadbackBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxStructuredBuffer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingSceneBuilder.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <span>
#include <vector>

namespace Engine {

	// front
	class GraphicsCore;
	class ECSWorld;

	//============================================================================
	//	MeshSubMeshPicker class
	// メッシュのサブメッシュ単位でのピック処理クラス
	//============================================================================
	class MeshSubMeshPicker {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		MeshSubMeshPicker() {
			tlasSlot_       = pickBindCache_.AddSlotByRegister(ShaderBindingKind::AccelStruct, 0, 0);
			outputUAVSlot_  = pickBindCache_.AddSlotByRegister(ShaderBindingKind::UAV,         0, 0);
			pickingCBVSlot_ = pickBindCache_.AddSlotByRegister(ShaderBindingKind::CBV,         0, 0);
		}
		~MeshSubMeshPicker() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// 前フレームで仕込んだ結果をフレーム頭で消費する
		void ConsumePendingResult(ECSWorld* world, EditorState& editorState);

		// シービュー左クリック時に実行
		void ExecutePick(GraphicsCore& graphicsCore, const ResolvedRenderView& view, const Vector2& inputPixel,
			std::span<const MeshSubMeshPickRecord> pickRecords, ID3D12Resource* tlasResource);

		// 終了処理
		void Finalize();

		//--------- accessor -----------------------------------------------------

	private:
		//============================================================================
		//	private Methods
		//============================================================================
		//--------- structure ----------------------------------------------------

		// ピック処理に必要な情報をまとめた構造体
		struct PickingData {

			uint32_t inputPixelX = 0;
			uint32_t inputPixelY = 0;
			uint32_t textureWidth = 0;
			uint32_t textureHeight = 0;

			// カメラ情報
			Matrix4x4 inverseViewProjection = Matrix4x4::Identity();
			Vector3 cameraWorldPos = Vector3::AnyInit(0.0f);

			// ピック用のレイの最大距離
			float rayMax = 10000.0f;
		};
		// ピック結果
		struct PickResult {

			uint32_t instanceID = 0xFFFFFFFFu;
		};

		//--------- variables ----------------------------------------------------

		// 無効なピックインスタンスID
		static constexpr uint32_t kInvalidPickInstanceID = 0xFFFFFFFFu;

		// CS用のパイプライン
		PipelineState pipeline_{};

		// ルート引数スロットのキャッシュ（TLAS/UAV/CBVをレジスタで解決）
		PipelineBindingCache pickBindCache_{};
		PipelineBindingCache::SlotID tlasSlot_       = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID outputUAVSlot_  = PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID pickingCBVSlot_ = PipelineBindingCache::kInvalidSlot;

		// バッファ
		DxConstBuffer<PickingData> pickingBuffer_{};
		DxStructuredBuffer<PickResult> outputBuffer_{};
		DxReadbackBuffer<PickResult> readbackBuffer_{};

		// ピッキングデータ
		std::vector<MeshSubMeshPickRecord> pendingRecords_{};

		// 出力バッファの現在の状態
		D3D12_RESOURCE_STATES outputState_ = D3D12_RESOURCE_STATE_COMMON;

		// 初期化済みかどうか
		bool initialized_ = false;
		bool pendingReadback_ = false;
	};
} // Engine