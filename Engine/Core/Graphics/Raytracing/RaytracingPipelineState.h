#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/ComPtr.h>
#include <Engine/Core/Graphics/Asset/RenderPipelineAsset.h>
#include <Engine/Core/Graphics/Asset/ShaderAsset.h>
#include <Engine/Core/Graphics/Pipeline/Stage/ShaderReflection.h>
#include <Engine/Core/Graphics/DxObject/DxShaderCompiler.h>

// directX
#include <d3d12.h>
#include <Externals/DirectX12/d3dx12.h>

namespace Engine {

	//============================================================================
	//	RaytracingPipelineState class
	//	レイトレーシング実行に必要なパイプラインステートを管理するクラス
	//============================================================================
	class RaytracingPipelineState {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RaytracingPipelineState() = default;
		~RaytracingPipelineState() = default;

		//--------- constants ----------------------------------------------------

		static constexpr UINT64 kHandleSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		static constexpr UINT64 kRecordStride = ((kHandleSize + (D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1)) & ~(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1));
		static constexpr UINT64 kTableAlign = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

		static constexpr UINT64 kRayGenOffset = 0 * kTableAlign;
		static constexpr UINT64 kMissOffset = 1 * kTableAlign;
		static constexpr UINT64 kHitGroupOffset = 2 * kTableAlign;

		static constexpr UINT kRootIndexTLAS = 0;
		static constexpr UINT kRootIndexSourceColor = 1;
		static constexpr UINT kRootIndexSourceDepth = 2;
		static constexpr UINT kRootIndexSourceNormal = 3;
		static constexpr UINT kRootIndexSourcePosition = 4;
		static constexpr UINT kRootIndexSceneInstances = 5;
		static constexpr UINT kRootIndexSceneSubMeshes = 6;
		static constexpr UINT kRootIndexDestUAV = 7;
		static constexpr UINT kRootIndexViewCBV = 8;

		//--------- functions ----------------------------------------------------

		// パイプライン作成
		void Create(ID3D12Device8* device, DxShaderCompiler* compiler,
			const PipelineVariantDesc& variant, const ShaderAsset& shaderAsset);

		// レイトレーシングのディスパッチ記述子を構築
		D3D12_DISPATCH_RAYS_DESC BuildDispatchDesc(uint32_t width, uint32_t height, uint32_t depth = 1) const;

		//--------- accessor -----------------------------------------------------

		ID3D12StateObject* GetStateObject() const { return stateObject_.Get(); }
		ID3D12RootSignature* GetRootSignature() const { return globalRootSignature_.Get(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// パイプライン
		ComPtr<ID3D12StateObject> stateObject_;
		ComPtr<ID3D12StateObjectProperties> stateProps_;
		ComPtr<ID3D12RootSignature> globalRootSignature_;

		// シェーダーテーブル
		ComPtr<ID3D12Resource> shaderTable_;
		UINT shaderTableSize_ = 0;

		//--------- functions ----------------------------------------------------

		// グローバルルートシグネチャの構築
		void BuildGlobalRootSignature(ID3D12Device8* device);
		// レイトレーシングパイプラインステートの構築
		void BuildStateObject(ID3D12Device8* device, DxShaderCompiler* compiler,
			const PipelineVariantDesc& variant, const ShaderAsset& shaderAsset);
		// シェーダーテーブルの構築
		void BuildShaderTable(ID3D12Device8* device, const std::wstring& rayGenExport,
			const std::wstring& missExport, const std::wstring& hitGroupExport);
	};
} // Engine