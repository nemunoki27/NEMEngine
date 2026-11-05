#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/ImmutableVertexBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/ImmutableIndexBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderTransformHistory.h>

// c++
#include <memory>
#include <span>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	SpriteBatchResources structures
	//============================================================================
	// 頂点データ
	struct SpriteVertex {

		Vector2 position;
		Vector2 texcoord;
	};
	// 定数バッファ
	struct SpriteViewConstants {

		Matrix4x4 viewProjection = Matrix4x4::Identity();
		Matrix4x4 unjitteredViewProjection = Matrix4x4::Identity();
		Matrix4x4 previousViewProjection = Matrix4x4::Identity();
		float materialPadding = 0.0f;
		float _viewPad[3]{};
	};
	// 頂点シェーダインスタンスデータ
	struct SpriteVSInstanceData {

		// サイズ
		Vector2 size = Vector2::AnyInit(1.0f);
		// ピボット
		Vector2 pivot = Vector2::AnyInit(0.5f);

		// ワールド行列
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
		Matrix4x4 previousWorldMatrix = Matrix4x4::Identity();
	};
	// ピクセルシェーダインスタンスデータ
	struct SpritePSInstanceData {

		// UV行列
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
	};

	//============================================================================
	//	SpriteBatchResources class
	//	スプライトをインスタンシングで描画するためのリソースを管理するクラス
	//============================================================================
	class SpriteBatchResources {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SpriteBatchResources() = default;
		~SpriteBatchResources() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// データ更新
		void UpdateView(const ResolvedRenderView& view, RenderCameraDomain cameraDomain, const Matrix4x4& previousViewProjection);
		void UploadInstances(const ResolvedRenderView& view,
			const RenderSceneBatch& batch, const std::span<const RenderItem* const>& items, RenderTransformHistory& history);

		//--------- accessor -----------------------------------------------------

		// VBVとIBVを取得する
		const D3D12_VERTEX_BUFFER_VIEW& GetVBV() const { return vertexBuffer_.GetVertexBufferView(); }
		const D3D12_INDEX_BUFFER_VIEW& GetIBV() const { return indexBuffer_.GetIndexBufferView(); }
		// GPUアドレスを取得する
		D3D12_GPU_VIRTUAL_ADDRESS GetViewGPUAddress() const { return view_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetInstanceVSGPUAddress() const { return vsData_.GetGPUAddress(); }
		D3D12_GPU_VIRTUAL_ADDRESS GetInstancePSGPUAddress() const { return psData_.GetGPUAddress(); }

		// 描画バウンディング名を取得する
		std::string_view GetViewBindingName() const { return view_.GetBindingName(); }
		std::string_view GetInstanceVSBindingName() const { return vsData_.GetBindingName(); }
		std::string_view GetInstancePSBindingName() const { return psData_.GetBindingName(); }

		uint32_t GetInstanceCount() const { return instanceCount_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// バッファ
		ImmutableVertexBuffer<SpriteVertex> vertexBuffer_;
		ImmutableIndexBuffer indexBuffer_;

		ViewConstantBuffer<SpriteViewConstants> view_{ "ViewConstants" };
		StructuredInstanceBuffer<SpriteVSInstanceData> vsData_{ "gVSInstances" };
		StructuredInstanceBuffer<SpritePSInstanceData> psData_{ "gPSInstances" };

		// 毎バッチ再利用するデータ
		std::vector<SpriteVSInstanceData> vsScratch_{};
		std::vector<SpritePSInstanceData> psScratch_{};

		// インスタンス数
		uint32_t instanceCount_ = 0;

		// 初期化状態
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 頂点バッファとインデックスバッファを作成する
		void CreateQuadBuffers(ID3D12Device* device, BufferUploadService& uploadService);
	};
} // Engine

