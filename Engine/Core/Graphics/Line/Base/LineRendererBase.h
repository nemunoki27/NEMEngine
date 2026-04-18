#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/Pipeline/Bind/GraphicsRootBinder.h>
#include <Engine/Core/Graphics/Render/View/RenderViewTypes.h>
#include <Engine/Core/Graphics/Render/Texture/MultiRenderTarget.h>
#include <Engine/Core/Graphics/GPUBuffer/DxConstBuffer.h>
#include <Engine/Core/Graphics/GPUBuffer/VertexBuffer.h>
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/MathLib/Math.h>

namespace Engine {

	//============================================================================
	//	LineRendererBase class
	//	ライン描画を行う基底クラス
	//============================================================================
	template <typename T>
	class LineRendererBase {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		LineRendererBase() = default;
		virtual ~LineRendererBase() = default;

		// 初期化
		void Init(GraphicsCore& graphicsCore, RenderCameraDomain cameraDomain);

		// フレーム開始処理
		void BeginFrame();

		// ライン描画
		// Vector2/Vector3で特殊化
		void DrawLine(const T& /*pointA*/, const T& /*pointB*/, const Color& /*color*/, float /*thickness*/ = 1.0f);
		void DrawLine(const T& /*pointA*/, const Color& colorA, float /*thicknessA*/,
			const T& /*pointB*/, const Color& /*colorB*/, float /*thicknessB*/);

		// 描画
		void RenderSceneView(GraphicsCore& graphicsCore, const ResolvedRenderView& view, MultiRenderTarget& surface);

		//--------- accessor -----------------------------------------------------

		// ライン最大数
		uint32_t GetMaxLineCount() const { return kMaxLineCount_; }
		// 現在積まれているライン数
		uint32_t GetCurrentLineCount() const { return static_cast<uint32_t>(vertices_.size() / 2); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// 頂点情報
		struct LineVertex {

			Vector3 position;
			// ライン半幅
			float thickness = 1.0f;

			Color color;
		};

		// 定数バッファ
		struct LinePassConstants {

			Matrix4x4 viewMatrix;
			Matrix4x4 projectionMatrix;

			Vector2 viewportSize = Vector2::AnyInit(1.0f);
			float nearClip = 0.01f;
			float feather = 1.25f;
			float padding = 0.0f;
		};

		//--------- variables ----------------------------------------------------

		// ライン最大数
		static constexpr uint32_t kMaxLineCount_ = 0xffff;
		static constexpr uint32_t kMaxVertexCount_ = kMaxLineCount_ * 2;
		static constexpr float kLineAAFeather_ = 1.25f;

		// パイプライン
		PipelineState pipeline_{};

		// バッファ
		VertexBuffer<LineVertex> vertexBuffer_{};
		DxConstBuffer<LinePassConstants> passBuffer_{};

		// 描画するラインの頂点情報
		std::vector<LineVertex> vertices_{};

		// 使用するカメラの種類
		RenderCameraDomain cameraDomain_{};

		//--------- functions ----------------------------------------------------

		// シーンカメラを取得する
		const ResolvedCameraView* FindSceneCamera(const ResolvedRenderView& view) const;

		// 派生ライン描画呼び出し
		virtual void DrawLineImpl(GraphicsCore& /*graphicsCore*/,
			const ResolvedCameraView* /*camera*/, MultiRenderTarget& /*surface*/) {}
	};

	//============================================================================
	//	LineRendererBase templateMethods
	//============================================================================

	template<typename T>
	inline void LineRendererBase<T>::Init(GraphicsCore& graphicsCore, RenderCameraDomain cameraDomain) {

		cameraDomain_ = cameraDomain;

		ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
		DxShaderCompiler* compiler = graphicsCore.GetDXObject().GetDxShaderCompiler();

		// パイプラインの生成
		GraphicsPipelineDesc desc{};
		desc.type = PipelineType::Geometry;

		// シェーダー設定
		desc.preRaster.file = "Builtin/Line/lineGeometry.VS.hlsl";
		desc.preRaster.entry = "main";
		desc.preRaster.profile = "vs_6_0";
		desc.geometry.file = "Builtin/Line/lineGeometry.GS.hlsl";
		desc.geometry.entry = "main";
		desc.geometry.profile = "gs_6_0";
		desc.pixel.file = "Builtin/Line/lineGeometry.PS.hlsl";
		desc.pixel.entry = "main";
		desc.pixel.profile = "ps_6_0";

		// ラスタライズ設定
		desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;

		// 深度ステンシル設定
		desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		desc.depthStencil.DepthEnable = TRUE;
		desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		desc.depthStencil.StencilEnable = FALSE;

		desc.sampleDesc = { 1, 0 };
		desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

		// ビューのデフォルトサーフェスに合わせる
		desc.numRenderTargets = 1;
		desc.rtvFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

		// パイプラインの生成
		bool created = pipeline_.CreateGraphics(device, compiler, desc);
		Assert::Call(created, "DebugLineRenderer pipeline create failed");

		// バッファの生成
		vertexBuffer_.CreateBuffer(device, kMaxVertexCount_);
		passBuffer_.CreateBuffer(device);

		// 頂点配列の容量を最大頂点数に合わせる
		vertices_.reserve(kMaxVertexCount_);
	}

	template<typename T>
	inline void LineRendererBase<T>::BeginFrame() {

		vertices_.clear();
	}

	template<typename T>
	inline void LineRendererBase<T>::RenderSceneView(GraphicsCore& graphicsCore,
		const ResolvedRenderView& view, MultiRenderTarget& surface) {

		// シーンカメラを取得
		const ResolvedCameraView* camera = FindSceneCamera(view);
		if (!camera) {
			return;
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		auto* commandList = dxCommand->GetCommandList();

		dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

		// 派生クラスのライン描画呼び出し
		DrawLineImpl(graphicsCore, camera, surface);

		// デバッグラインが無ければここで終了
		if (vertices_.empty()) {
			return;
		}

		// 現在サーフェスに重ねて描画
		surface.TransitionForRender(*dxCommand);
		surface.Bind(*dxCommand);

		// GPUリソース更新
		vertexBuffer_.TransferData(vertices_);

		// 定数バッファ更新
		LinePassConstants constants{};
		constants.viewMatrix = camera->matrices.viewMatrix;
		constants.projectionMatrix = camera->matrices.projectionMatrix;
		constants.viewportSize = Vector2(static_cast<float>(surface.GetWidth()), static_cast<float>(surface.GetHeight()));
		constants.nearClip = camera->nearClip;
		constants.feather = kLineAAFeather_;
		passBuffer_.TransferData(constants);

		// パイプライン設定
		commandList->SetGraphicsRootSignature(pipeline_.GetRootSignature());
		commandList->SetPipelineState(pipeline_.GetGraphicsPipeline(BlendMode::Normal));

		// IAステージ設定
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		commandList->IASetVertexBuffers(0, 1, &vertexBuffer_.GetVertexBufferView());

		// ルートパラメータのバインド
		GraphicsRootBinder binder{ pipeline_ };
		const GraphicsBindItem bindItems[] = {
			{ {}, GraphicsBindValueType::CBV, passBuffer_.GetResource()->GetGPUVirtualAddress(), {}, 0, 0 },
		};
		binder.Bind(commandList, bindItems);

		// 描画
		commandList->DrawInstanced(static_cast<UINT>(vertices_.size()), 1, 0, 0);
	}

	template<typename T>
	inline const ResolvedCameraView* LineRendererBase<T>::FindSceneCamera(const ResolvedRenderView& view) const {

		const ResolvedCameraView* camera = view.FindCamera(cameraDomain_);
		if (camera) {
			return camera;
		}
		return nullptr;
	}
} // Engine