#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxObject/DxShaderCompiler.h>
#include <Engine/Core/Graphics/Pipeline/Stage/AutoRootSignatureBuilder.h>
#include <Engine/Core/Graphics/Pipeline/Stage/AutoInputLayoutBuilder.h>
#include <Engine/Core/Graphics/Pipeline/Stage/BlendState.h>

// directX
#include <Externals/DirectX12/d3dx12.h>

namespace Engine {

	//============================================================================
	//	PipelineState structures
	//============================================================================

	// シェーダーコンパイルに必要な情報
	struct ShaderCompileDesc {

		std::string file;
		std::string entry = "main";
		std::string profile;
	};
	// グラフィックスパイプラインの生成に必要な情報
	struct GraphicsPipelineDesc {

		// シェーダー名
		ShaderCompileDesc preRaster;
		ShaderCompileDesc geometry;
		ShaderCompileDesc pixel;
		ShaderCompileDesc amplification;
		// 種類
		PipelineType type = PipelineType::Vertex;

		// サンプラー情報
		std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;

		// ラスタライズ設定
		D3D12_RASTERIZER_DESC rasterizer{};
		// 深度ステンシル設定
		D3D12_DEPTH_STENCIL_DESC depthStencil{};
		DXGI_SAMPLE_DESC sampleDesc{ 1, 0 };
		// プリミティブトポロジーの種類
		D3D12_PRIMITIVE_TOPOLOGY_TYPE topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		// レンダーターゲットのフォーマット
		DXGI_FORMAT rtvFormats[8]{};
		// レンダーターゲットの数
		UINT numRenderTargets = 1;
		// 深度ステンシルのフォーマット
		DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN;
	};
	// CSを使用するコンピュートパイプラインの生成に必要な情報
	struct ComputePipelineDesc {

		// シェーダー名
		ShaderCompileDesc compute;

		// サンプラー情報
		std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
	};

	//============================================================================
	//	PipelineState class
	//	パイプラインステートオブジェクトの生成に必要な情報を保持するクラス
	//============================================================================
	class PipelineState {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PipelineState() = default;
		~PipelineState() = default;

		// パイプラインステートオブジェクトの生成
		bool CreateGraphics(ID3D12Device8* device, DxShaderCompiler* compiler, const GraphicsPipelineDesc& desc);
		bool CreateCompute(ID3D12Device8* device, DxShaderCompiler* compiler, const ComputePipelineDesc& desc);

		//--------- accessor -----------------------------------------------------

		// ルート引数の配置情報の取得
		const RootBindingLocation* FindBinding(ShaderBindingKind kind, UINT bindPoint, UINT space = 0) const;
		const RootBindingLocation* FindBindingByName(const std::string_view& name, ShaderBindingKind kind) const;

		// ルートシグネイチャとパイプラインステートオブジェクトの取得
		ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }
		ID3D12PipelineState* GetGraphicsPipeline(BlendMode blendMode) const;
		ID3D12PipelineState* GetComputePipeline() const { return computePipeline_.Get(); }

		// スレッドグループサイズの取得
		UINT GetThreadGroupX() const { return threadGroupX_; }
		UINT GetThreadGroupY() const { return threadGroupY_; }
		UINT GetThreadGroupZ() const { return threadGroupZ_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// register/space/kindから引くためのキー
		struct BindingRegisterKey {

			ShaderBindingKind kind = ShaderBindingKind::CBV;
			UINT bindPoint = 0;
			UINT space = 0;

			bool operator==(const BindingRegisterKey& rhs) const noexcept {
				return kind == rhs.kind && bindPoint == rhs.bindPoint && space == rhs.space;
			}
		};
		struct BindingRegisterKeyHash {
			size_t operator()(const BindingRegisterKey& key) const noexcept {
				size_t h1 = std::hash<uint32_t>{}(static_cast<uint32_t>(key.kind));
				size_t h2 = std::hash<UINT>{}(key.bindPoint);
				size_t h3 = std::hash<UINT>{}(key.space);
				size_t result = h1;
				result ^= h2 + 0x9e3779b9 + (result << 6) + (result >> 2);
				result ^= h3 + 0x9e3779b9 + (result << 6) + (result >> 2);
				return result;
			}
		};

		//--------- variables ----------------------------------------------------

		// ルートバインドの種類の数
		static constexpr size_t kBindingKindCount = static_cast<size_t>(ShaderBindingKind::AccelStruct) + 1;

		// ルートシグネイチャ
		ComPtr<ID3D12RootSignature> rootSignature_;
		// パイプラインステートオブジェクト
		std::array<ComPtr<ID3D12PipelineState>, kBlendModeCount> graphicsPipelines_;
		ComPtr<ID3D12PipelineState> computePipeline_;

		// ルート引数の配置情報
		std::vector<RootBindingLocation> bindings_;

		// register指定検索用
		std::unordered_map<BindingRegisterKey, size_t, BindingRegisterKeyHash> registerBindingTable_;
		// 名前検索用
		std::array<std::unordered_map<std::string_view, size_t>, kBindingKindCount> nameBindingTables_;

		// スレッドグループサイズ
		UINT threadGroupX_ = 1;
		UINT threadGroupY_ = 1;
		UINT threadGroupZ_ = 1;

		//--------- functions ----------------------------------------------------

		// 検索テーブルを再構築する
		void RebuildBindingLookupTables();
	};
} // Engine