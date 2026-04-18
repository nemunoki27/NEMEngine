#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/Pipeline/Bind/ComputeRootBinder.h>
#include <Engine/Core/Graphics/GPUBuffer/DxConstBuffer.h>
#include <Engine/Core/Graphics/GPUBuffer/DxReadbackBuffer.h>
#include <Engine/Core/Graphics/GPUBuffer/DxStructuredBuffer.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/Graphics/Raytracing/RaytracingSceneBuilder.h>
#include <Engine/Editor/EditorState.h>
#include <Engine/MathLib/Vector2.h>

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
		//========================================================================
		//	public Methods
		//========================================================================

		MeshSubMeshPicker() = default;
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
		//========================================================================
		//	private Methods
		//========================================================================

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