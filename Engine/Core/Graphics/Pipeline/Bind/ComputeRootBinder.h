#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/Pipeline/Bind/RootBindingCommandHelper.h>

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	ComputeRootBinder structures
	//============================================================================

	// ルートパラメータの種類
	enum class ComputeBindValueType {

		CBV,
		SRV,
		UAV,
		AccelStruct,
	};
	// ルートパラメータのバインド情報
	struct ComputeBindItem {

		std::string_view name{};
		ComputeBindValueType type = ComputeBindValueType::CBV;
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE descriptor{};

		UINT bindPoint = 0;
		UINT space = 0;
	};

	//============================================================================
	//	ComputeRootBinder class
	//	コンピュートパイプラインのルートパラメータのバインドを管理するクラス
	//============================================================================
	class ComputeRootBinder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ComputeRootBinder(const PipelineState& pipeline) : pipeline_(&pipeline) {}
		~ComputeRootBinder() = default;

		//--------- accessor -----------------------------------------------------

		void Bind(ID3D12GraphicsCommandList* commandList,
			const std::span<const ComputeBindItem>& items) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const PipelineState* pipeline_ = nullptr;

		//--------- functions ----------------------------------------------------

		const RootBindingLocation* ResolveBinding(const ComputeBindItem& item) const;
	};
} // Engine