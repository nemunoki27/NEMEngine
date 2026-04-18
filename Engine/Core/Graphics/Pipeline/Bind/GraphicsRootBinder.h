#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/PipelineState.h>
#include <Engine/Core/Graphics/Pipeline/Bind/RootBindingCommandHelper.h>

// c++
#include <string_view>
#include <span>

namespace Engine {

	//============================================================================
	//	GraphicsRootBinder structures
	//============================================================================

	// ルートパラメータの種類
	enum class GraphicsBindValueType {

		CBV,
		SRV,
		AccelStruct,
	};
	// ルートパラメータのバインド情報
	struct GraphicsBindItem {

		std::string_view name{};
		GraphicsBindValueType type = GraphicsBindValueType::CBV;
		D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
		D3D12_GPU_DESCRIPTOR_HANDLE descriptor{};

		UINT bindPoint = 0;
		UINT space = 0;
	};

	//============================================================================
	//	GraphicsRootBinder class
	//	グラフィックスパイプラインのルートパラメータのバインドを管理するクラス
	//============================================================================
	class GraphicsRootBinder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		GraphicsRootBinder(const PipelineState& pipeline) : pipeline_(&pipeline) {}
		~GraphicsRootBinder() = default;

		void Bind(ID3D12GraphicsCommandList* commandList,
			const std::span<const GraphicsBindItem>& items) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const PipelineState* pipeline_ = nullptr;

		//--------- functions ----------------------------------------------------

		const RootBindingLocation* ResolveBinding(const GraphicsBindItem& item) const;
	};
} // Engine