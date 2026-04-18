#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GPUBuffer/VertexBuffer.h>
#include <Engine/Core/Graphics/GPUBuffer/IndexBuffer.h>
#include <Engine/Core/Graphics/Render/Backend/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Graphics/Render/Backend/Common/ViewConstantBuffer.h>
#include <Engine/Core/Graphics/Render/Queue/RenderQueue.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/MathLib/Color.h>

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
	};
	// 頂点シェーダインスタンスデータ
	struct SpriteVSInstanceData {

		// サイズ
		Vector2 size = Vector2::AnyInit(1.0f);
		// ピボット
		Vector2 pivot = Vector2::AnyInit(0.5f);

		// ワールド行列
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
	};
	// ピクセルシェーダインスタンスデータ
	struct SpritePSInstanceData {

		// 色
		Color color = Color::White();

		// UV行列
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
	};

	//============================================================================
	//	SpriteBatchResources class
	//	スプライトをインスタンシングで描画するためのリソースを管理するクラス
	//============================================================================
	class SpriteBatchResources {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SpriteBatchResources() = default;
		~SpriteBatchResources() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// データ更新
		void UpdateView(const ResolvedRenderView& view);
		void UploadInstances(const RenderSceneBatch& batch, const std::span<const RenderItem* const>& items);

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
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// バッファ
		VertexBuffer<SpriteVertex> vertexBuffer_;
		IndexBuffer indexBuffer_;

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
		void CreateQuadBuffers(ID3D12Device* device);
	};
} // Engine