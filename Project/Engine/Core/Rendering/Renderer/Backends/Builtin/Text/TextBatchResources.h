#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/ImmutableVertexBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/ImmutableIndexBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/StructuredInstanceBuffer.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Foundation/Math/Math.h>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	TextBatchResources structures
	//============================================================================
	// 頂点データ
	struct TextVertex {

		Vector2 position;
		Vector2 texcoord;
	};
	// 定数バッファ
	struct TextViewConstants {

		Matrix4x4 viewProjection = Matrix4x4::Identity();
	};
	// 頂点シェーダインスタンスデータ
	struct TextVSInstanceData {

		// 矩形の最小座標と最大座標
		Vector2 rectMin{};
		Vector2 rectMax{};
		// UVの最小座標と最大座標
		Vector2 uvMin{};
		Vector2 uvMax{};
		// マテリアルUVの最小座標と最大座標
		Vector2 materialUVMin{};
		Vector2 materialUVMax = Vector2::AnyInit(1.0f);

		// ワールド行列
		Matrix4x4 worldMatrix = Matrix4x4::Identity();
	};
	// ピクセルシェーダインスタンスデータ
	// HLSL側のPSInstanceとオフセットを完全一致させるため並びとpaddingを固定する
	struct TextPSInstanceData {

		// フォントアトラスのサイズ
		Vector2 atlasSize = Vector2::AnyInit(1.0f);
		// ピクセル単位の文字の範囲
		float pxRange = 8.0f;
		// 16バイト境界へ揃えるための詰め物
		float padding0 = 0.0f;
		// UV行列
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
	};

	//============================================================================
	//	TextBatchResources class
	//	テキストを描画するためのリソースを管理するクラス
	//============================================================================
	class TextBatchResources {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		TextBatchResources() = default;
		~TextBatchResources() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// 描画に使用するビューを更新する、2DはOrthographic 3DはPerspectiveのカメラを使う
		void UpdateView(const ResolvedRenderView& view, RenderCameraDomain cameraDomain);
		void UploadGlyphs(const std::vector<TextVSInstanceData>& vsGlyphs, const std::vector<TextPSInstanceData>& psGlyphs);

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
		std::string_view GetGlyphVSBindingName() const { return vsData_.GetBindingName(); }
		std::string_view GetGlyphPSBindingName() const { return psData_.GetBindingName(); }

		uint32_t GetInstanceCount() const { return instanceCount_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// バッファ
		ImmutableVertexBuffer<TextVertex> vertexBuffer_;
		ImmutableIndexBuffer indexBuffer_;

		ViewConstantBuffer<TextViewConstants> view_{ "ViewConstants" };
		StructuredInstanceBuffer<TextVSInstanceData> vsData_{ "gVSInstances" };
		StructuredInstanceBuffer<TextPSInstanceData> psData_{ "gPSInstances" };

		// インスタンス数
		uint32_t instanceCount_ = 0;

		// 初期化状態
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 頂点バッファとインデックスバッファを作成する
		void CreateQuadBuffers(ID3D12Device* device, BufferUploadService& uploadService);
	};
} // Engine

