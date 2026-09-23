#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Rendering/Assets/ShaderAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/AutoRootSignatureBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderCompiler.h>

// directX
#include <d3d12.h>
#include <Externals/DirectX12/d3dx12.h>

namespace Engine {

	class GraphicsResourceRetirement;

	//============================================================================
	//	RaytracingPipelineState class
	//	レイトレーシング実行に必要なパイプラインステートを管理するクラス
	//============================================================================
	class RaytracingPipelineState {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RaytracingPipelineState() = default;
		~RaytracingPipelineState();
		RaytracingPipelineState(const RaytracingPipelineState&) = delete;
		RaytracingPipelineState& operator=(const RaytracingPipelineState&) = delete;

		//--------- constants ----------------------------------------------------

		static constexpr UINT64 kHandleSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
		static constexpr UINT64 kRecordStride =
			((kHandleSize + (D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1)) &
				~(D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT - 1));
		static constexpr UINT64 kTableAlign = D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT;

		//--------- functions ----------------------------------------------------

		// GPU完了までState ObjectとShader Tableを保持する
		void RetireGPUObjects(GraphicsResourceRetirement& retirement) const;

		// パイプライン作成

		// レイトレーシングのディスパッチ記述子を構築
		D3D12_DISPATCH_RAYS_DESC BuildDispatchDesc(uint32_t width,
			uint32_t height, uint32_t depth = 1,
			uint32_t rayGenerationIndex = 0) const;

		// GPU使用後の回収先を設定する
		void SetRetirementQueue(GraphicsResourceRetirement& retirement);

		//--------- accessor -----------------------------------------------------

		ID3D12StateObject* GetStateObject() const { return stateObject_.Get(); }
		ID3D12RootSignature* GetRootSignature() const { return globalRootSignature_.Get(); }
		const std::vector<RootBindingLocation>& GetBindings() const { return bindings_; }
		const ShaderReflectionInfo& GetReflection() const { return reflection_; }
		const RootBindingLocation* FindBindingByName(
			std::string_view name, ShaderBindingKind kind) const;
		uint64_t GetUniqueID() const { return uniqueID_; }
		uint32_t GetRayGenerationCount() const { return rayGenerationCount_; }
		bool IsValid() const { return stateObject_ && stateProps_ && shaderTable_; }
	private:
		friend class RaytracingPipelineBuilder;
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		GraphicsResourceRetirement* retirement_ = nullptr;

		// パイプライン
		ComPtr<ID3D12StateObject> stateObject_;
		ComPtr<ID3D12StateObjectProperties> stateProps_;
		ComPtr<ID3D12RootSignature> globalRootSignature_;
		std::vector<RootBindingLocation> bindings_{};
		ShaderReflectionInfo reflection_{};

		// シェーダーテーブル
		ComPtr<ID3D12Resource> shaderTable_;
		UINT shaderTableSize_ = 0;
		UINT64 rayGenerationTableOffset_ = 0;
		UINT64 missTableOffset_ = 0;
		UINT64 hitGroupTableOffset_ = 0;
		UINT64 callableTableOffset_ = 0;
		uint32_t rayGenerationCount_ = 0;
		uint32_t missCount_ = 0;
		uint32_t hitGroupCount_ = 0;
		uint32_t callableCount_ = 0;
		uint64_t uniqueID_ = NextUniqueID();

		//--------- functions ----------------------------------------------------

		// グローバルルートシグネチャの構築

		// レイトレーシングパイプラインステートの構築

		// シェーダーテーブルの構築

		// Pipelineの識別番号を採番する
		static uint64_t NextUniqueID();
	};
} // Engine
