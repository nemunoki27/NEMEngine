#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Pipelines/BuiltinShaderSource.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/DxObject/Buffers/VertexBuffer.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <memory>
#include <array>
#include <vector>

namespace Engine {

	//============================================================================
	//	LineRendererBase class
	//	ライン描画を行う基底クラス
	//============================================================================
	template <typename T>
	class LineRendererBase {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		LineRendererBase() {
			lineCBVSlot_ = lineBindCache_.AddSlotByRegister(ShaderBindingKind::CBV, 0, 0);
		}
		virtual ~LineRendererBase() {
			// 描画中に確保したGPUバッファを持つRenderResourceを明示resetする
			for (auto& frameResources : renderResources_) {
				for (auto& resource : frameResources) {
					resource.reset();
				}
				frameResources.clear();
			}
		}

		// 初期化
		void Init(GraphicsCore& graphicsCore, RenderCameraDomain cameraDomain);

		// フレーム開始処理
		void BeginFrame();

		// ライン描画
		// Vector2/Vector3で特殊化
		void DrawLine(const T& /*pointA*/, const T& /*pointB*/, const Color4& /*color*/, float /*thickness*/ = 1.0f);
		void DrawLine(const T& /*pointA*/, const Color4& colorA, float /*thicknessA*/,
			const T& /*pointB*/, const Color4& /*colorB*/, float /*thicknessB*/);

		// 描画
		void RenderSceneView(GraphicsCore& graphicsCore, const ResolvedRenderView& view,
			MultiRenderTarget& surface, bool drawQueuedLines = true);

		//--------- accessor -----------------------------------------------------

		// ライン最大数
		uint32_t GetMaxLineCount() const { return kMaxLineCount_; }
		// 現在積まれているライン数
		uint32_t GetCurrentLineCount() const { return static_cast<uint32_t>(vertices_.size() / 2); }

		// 以降のDrawLineを深度オクルージョン対象バッチへ積むかどうか、衝突形状などメッシュに隠したい線に使う
		void SetOccludedMode(bool enable) { occludedMode_ = enable; }
		// 深度オクルージョン用のシーン深度を設定する、次のRenderSceneViewでだけ使い切る
		void SetOcclusionDepth(DepthTexture2D* depth) { occlusionDepth_ = depth; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 頂点情報
		struct LineVertex {

			Vector3 position;
			// ライン半幅
			float thickness = 1.0f;

			Color4 color;
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
		// 1回の描画で使用するGPUバッファ
		struct RenderResource {

			VertexBuffer<LineVertex> vertexBuffer;
			DxConstBuffer<LinePassConstants> passBuffer;
		};

		//--------- variables ----------------------------------------------------

		// ライン最大数
		static constexpr uint32_t kMaxLineCount_ = 0xffff;
		static constexpr uint32_t kMaxVertexCount_ = kMaxLineCount_ * 2;
		static constexpr float kLineAAFeather_ = 1.25f;

		// パイプライン
		std::unique_ptr<PipelineState> pipeline_{};
		// 深度オクルージョン用パイプライン、シーン深度でテストし書き込みはしない
		std::unique_ptr<PipelineState> occludedPipeline_{};

		// ラインパス定数バッファb0のスロットキャッシュ
		PipelineBindingCache lineBindCache_{};
		PipelineBindingCache::SlotID lineCBVSlot_ = PipelineBindingCache::kInvalidSlot;

		// 同じコマンドリスト内で複数回描画しても、後の描画内容で上書きしないためのバッファ
		std::array<std::vector<std::unique_ptr<RenderResource>>,
			kGraphicsFrameContextCount> renderResources_{};
		std::array<uint32_t,
			kGraphicsFrameContextCount> renderResourceIndices_{};

		// 描画するラインの頂点情報、常に手前に描くオーバーレイ線
		std::vector<LineVertex> vertices_{};
		// 深度オクルージョン対象のラインの頂点情報、メッシュに隠れる線
		std::vector<LineVertex> occludedVertices_{};
		// 現在のDrawLineをどちらのバッチへ積むか
		bool occludedMode_ = false;
		// 深度オクルージョン用のシーン深度、非所有でフレームごとに設定される
		DepthTexture2D* occlusionDepth_ = nullptr;

		// 使用するカメラの種類
		RenderCameraDomain cameraDomain_{};

		//--------- functions ----------------------------------------------------

		// シーンカメラを取得する
		const ResolvedCameraView* FindSceneCamera(const ResolvedRenderView& view) const;
		// 描画ごとのGPUバッファを取得する
		RenderResource& AllocateRenderResource(GraphicsCore& graphicsCore);
		// 現在のモードに応じた積み先の頂点バッチを返す
		std::vector<LineVertex>& ActiveVertices() { return occludedMode_ ? occludedVertices_ : vertices_; }
		// 1つの頂点バッチを指定パイプラインと深度で描画する、occlusionDepth指定時はそれをテスト用DSVに使う
		void RenderLineBatch(GraphicsCore& graphicsCore, const ResolvedCameraView* camera,
			MultiRenderTarget& surface, std::vector<LineVertex>& batch, PipelineState& pipeline,
			DepthTexture2D* occlusionDepth);

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
		desc.preRaster.file = BuiltinShaderSource::Line::GeometryVS;
		desc.preRaster.entry = "main";
		desc.preRaster.profile = "vs_6_0";
		desc.geometry.file = BuiltinShaderSource::Line::GeometryGS;
		desc.geometry.entry = "main";
		desc.geometry.profile = "gs_6_0";
		desc.pixel.file = BuiltinShaderSource::Line::GeometryPS;
		desc.pixel.entry = "main";
		desc.pixel.profile = "ps_6_0";

		// ラスタライズ設定
		desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;

		// 深度ステンシル設定
		desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		desc.depthStencil.DepthEnable = TRUE;
		desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		desc.depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		desc.depthStencil.StencilEnable = FALSE;

		desc.sampleDesc = { 1, 0 };
		desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

		// ビューのデフォルトサーフェスに合わせる
		desc.numRenderTargets = 1;
		desc.rtvFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

		// パイプラインの生成
		bool created = (pipeline_ = PipelineStateBuilder::CreateGraphics(device, compiler, desc)) != nullptr;
		Assert::Call(created, "DebugLineRendererのPipeline作成に失敗しました");

		// 深度オクルージョン用パイプライン、シーン深度でテストするが書き込みはしないので深度を壊さない
		desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		bool occludedCreated = (occludedPipeline_ = PipelineStateBuilder::CreateGraphics(device, compiler, desc)) != nullptr;
		Assert::Call(occludedCreated, "DebugLineRendererの遮蔽Pipeline作成に失敗しました");

		// 描画用バッファは同じフレーム内の描画回数に応じて確保する
		for (auto& resources : renderResources_) {
			resources.reserve(4);
		}

		// 頂点配列の容量を最大頂点数に合わせる
		vertices_.reserve(kMaxVertexCount_);
	}

	template<typename T>
	inline void LineRendererBase<T>::BeginFrame() {

		vertices_.clear();
		occludedVertices_.clear();
		occludedMode_ = false;
		occlusionDepth_ = nullptr;
		renderResourceIndices_[GraphicsFrameState::GetCurrentIndex()] = 0;
	}

	template<typename T>
	inline void LineRendererBase<T>::RenderSceneView(GraphicsCore& graphicsCore,
		const ResolvedRenderView& view, MultiRenderTarget& surface, bool drawQueuedLines) {

		// シーンカメラを取得
		const ResolvedCameraView* camera = FindSceneCamera(view);
		if (!camera) {
			return;
		}

		// 派生クラスのライン描画呼び出し
		DrawLineImpl(graphicsCore, camera, surface);

		if (!drawQueuedLines) {
			return;
		}

		// 通常のオーバーレイ線はサーフェスの深度に従う、基本は常に手前に描く
		if (pipeline_) {
			RenderLineBatch(graphicsCore, camera, surface, vertices_, *pipeline_, nullptr);
		}
		// 深度オクルージョン対象の線はシーン深度でテストしてメッシュに隠す
		if (occludedPipeline_) {
			RenderLineBatch(graphicsCore, camera, surface, occludedVertices_, *occludedPipeline_, occlusionDepth_);
		}
	}

	template<typename T>
	inline void LineRendererBase<T>::RenderLineBatch(GraphicsCore& graphicsCore,
		const ResolvedCameraView* camera, MultiRenderTarget& surface, std::vector<LineVertex>& batch,
		PipelineState& pipeline, DepthTexture2D* occlusionDepth) {

		// 積まれた線が無ければ描かない
		if (batch.empty()) {
			return;
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		auto* commandList = dxCommand->GetCommandList();

		// 現在サーフェスに重ねて描画
		surface.TransitionForRender(*dxCommand);
		RenderTexture2D* color = surface.GetColorTexture(0);
		if (!color) {
			batch.clear();
			return;
		}

		if (occlusionDepth) {

			// シーン深度でテストして線をメッシュに隠す、深度書き込みはZEROなので内容は壊さない
			occlusionDepth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			dxCommand->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()),
				occlusionDepth->GetDSVCPUHandle());
		} else if (DepthTexture2D* depth = surface.GetDepthTexture()) {

			dxCommand->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()),
				depth->GetDSVCPUHandle());
		} else {

			dxCommand->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()), std::nullopt);
		}
		dxCommand->SetViewportAndScissor(surface.GetWidth(), surface.GetHeight());

		// GPUリソース更新
		RenderResource& renderResource = AllocateRenderResource(graphicsCore);
		renderResource.vertexBuffer.TransferData(batch);

		// 定数バッファ更新
		LinePassConstants constants{};
		constants.viewMatrix = camera->matrices.viewMatrix;
		constants.projectionMatrix = camera->matrices.projectionMatrix;
		constants.viewportSize = Vector2(static_cast<float>(surface.GetWidth()), static_cast<float>(surface.GetHeight()));
		constants.nearClip = camera->nearClip;
		constants.feather = kLineAAFeather_;
		renderResource.passBuffer.TransferData(constants);

		// パイプライン設定
		commandList->SetGraphicsRootSignature(pipeline.GetRootSignature());
		commandList->SetPipelineState(pipeline.GetGraphicsPipeline(BlendMode::Normal));

		// IAステージ設定
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		commandList->IASetVertexBuffers(0, 1, &renderResource.vertexBuffer.GetVertexBufferView());

		// ルートパラメータのバインドでパイプラインが変わった時だけスロットを再解決する
		lineBindCache_.Sync(pipeline);
		if (lineBindCache_.Has(lineCBVSlot_)) {
			RootBindingCommand::SetGraphicsCBV(commandList, lineBindCache_.Get(lineCBVSlot_),
				renderResource.passBuffer.GetResource()->GetGPUVirtualAddress());
		}

		// 描画
		commandList->DrawInstanced(static_cast<UINT>(batch.size()), 1, 0, 0);
		batch.clear();
	}

	template<typename T>
	inline const ResolvedCameraView* LineRendererBase<T>::FindSceneCamera(const ResolvedRenderView& view) const {

		const ResolvedCameraView* camera = view.FindCamera(cameraDomain_);
		if (camera) {
			return camera;
		}
		return nullptr;
	}

	template<typename T>
	inline typename LineRendererBase<T>::RenderResource& LineRendererBase<T>::AllocateRenderResource(GraphicsCore& graphicsCore) {

		const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
		auto& resources = renderResources_[frameIndex];
		uint32_t& resourceIndex = renderResourceIndices_[frameIndex];
		if (resources.size() <= resourceIndex) {

			// GPU実行前のコマンドが参照しているバッファを、後続の描画で上書きしない
			auto resource = std::make_unique<RenderResource>();
			resource->vertexBuffer.CreateBuffer(graphicsCore.GetDXObject().GetDevice(), kMaxVertexCount_);
			resource->passBuffer.CreateBuffer(graphicsCore.GetDXObject().GetDevice());
			resources.emplace_back(std::move(resource));
		}
		return *resources[resourceIndex++];
	}
} // Engine
