#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/AutoRootSignatureBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Stage/AutoInputLayoutBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Stage/BlendState.h>

// directX
#include <Externals/DirectX12/d3dx12.h>
// c++
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	PipelineState structures
	//============================================================================
	// シェーダーコンパイルに必要な情報
	struct ShaderCompileDesc {

		std::string file;
		std::string entry = "main";
		std::string profile;
		AssetID shader{};
	};
	// 静的サンプラーの編集可能な設定
	struct PipelineStaticSamplerSettings {

		D3D12_FILTER filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		D3D12_TEXTURE_ADDRESS_MODE addressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		D3D12_TEXTURE_ADDRESS_MODE addressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		D3D12_TEXTURE_ADDRESS_MODE addressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		D3D12_STATIC_BORDER_COLOR borderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		D3D12_COMPARISON_FUNC comparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		uint32_t maxAnisotropy = 1;
		float mipLODBias = 0.0f;
		float minLOD = 0.0f;
		float maxLOD = D3D12_FLOAT32_MAX;
	};
	// シェーダー内SamplerState名をキーにした静的サンプラー上書き
	struct PipelineStaticSamplerOverrideSet {

		bool fillMissingSamplers = false;
		std::unordered_map<std::string, PipelineStaticSamplerSettings> byName;
	};
	// Reflectionと上書き設定から静的サンプラーを構築する
	std::vector<D3D12_STATIC_SAMPLER_DESC> BuildPipelineStaticSamplers(
		const ShaderReflectionInfo& reflection,
		const std::vector<D3D12_STATIC_SAMPLER_DESC>& baseSamplers,
		const PipelineStaticSamplerOverrideSet& overrides);
	// Pipelineキャッシュ用のサンプラー設定ハッシュを構築する
	uint64_t HashPipelineStaticSamplerOverrides(
		const PipelineStaticSamplerOverrideSet* samplerOverrides);
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
		PipelineStaticSamplerOverrideSet staticSamplerOverrides;

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
		PipelineStaticSamplerOverrideSet staticSamplerOverrides;
	};

	//============================================================================
	//	PipelineState class
	//	パイプラインステートオブジェクトの生成に必要な情報を保持するクラス
	//============================================================================
	class PipelineState {
		friend class PipelineStateBuilder;
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PipelineState() = default;
		~PipelineState() = default;

		PipelineState(const PipelineState&) = delete;
		PipelineState& operator=(const PipelineState&) = delete;

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
		// コンピュートシェーダーのリフレクション情報を取得する
		const ShaderReflectionInfo& GetComputeReflection() const { return computeReflection_; }
		// グラフィックス全ステージを統合したリフレクション情報を取得する、マテリアルパラメータ解決に使う
		const ShaderReflectionInfo& GetGraphicsReflection() const { return graphicsReflection_; }
		// パイプラインごとに一意なID、破棄後の同アドレス再利用でもキャッシュ誤ヒットを防ぐために使う
		uint64_t GetUniqueID() const { return uniqueID_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// register/space/kindから引くためのキー
		struct BindingRegisterKey {

			ShaderBindingKind kind = ShaderBindingKind::CBV;
			UINT bindPoint = 0;
			UINT space = 0;

			bool operator==(const BindingRegisterKey& rhs) const noexcept;
		};
		struct BindingRegisterKeyHash {
			size_t operator()(const BindingRegisterKey& key) const noexcept;
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
		// Compute用の定数バッファ構造をPostProcess側で参照する
		ShaderReflectionInfo computeReflection_{};
		// グラフィックス全ステージ統合のリフレクション情報、マテリアルパラメータ/SRV解決に使う
		ShaderReflectionInfo graphicsReflection_{};

		// パイプラインごとに一意なID、生成のたびに採番される
		uint64_t uniqueID_ = NextUniqueID();

		//--------- functions ----------------------------------------------------

		// 検索テーブルを再構築する
		void RebuildBindingLookupTables();

		// パイプライン生成のたびに一意なIDを採番する
		static uint64_t NextUniqueID();
	};
} // Engine
