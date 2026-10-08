#include "AutoRootSignatureBuilder.h"

//============================================================================
//	include
//============================================================================
#include "RootSignaturePlanning.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <string>
#include <utility>

namespace {

	// 検証済みのBinding種別をDescriptor Rangeへ変換する
	D3D12_DESCRIPTOR_RANGE_TYPE ToRangeType(Engine::ShaderBindingKind kind) {

		switch (kind) {
		case Engine::ShaderBindingKind::CBV: return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		case Engine::ShaderBindingKind::UAV: return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		case Engine::ShaderBindingKind::Sampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		default: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		}
	}
}

//============================================================================
//	AutoRootSignatureBuilder classMethods
//============================================================================
Engine::RootSignatureBuildResult Engine::AutoRootSignatureBuilder::Build(ID3D12Device* device, PipelineType pipelineType,
	const std::vector<const CompiledShader*>& shaders, const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers) {

	// 配置の失敗をGPU生成へ持ち越さない
	RootSignaturePlan plan = RootSignaturePlanning::Build(pipelineType, shaders, staticSamplers);
	if (!plan.IsValid()) {
		Logger::Output(LogType::Engine, spdlog::level::err, "[RootSignature] 配置に失敗しました: {}", plan.diagnostics);
		return {};
	}
	size_t tableCount = std::count_if(plan.bindings.begin(), plan.bindings.end(), [](const auto& binding) {

		return binding.parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	});
	// レンジ、ルート引数の配列を構築
	std::vector<D3D12_DESCRIPTOR_RANGE1> ranges(tableCount);
	std::vector<D3D12_ROOT_PARAMETER1> params(plan.bindings.size());
	size_t tableIndex = 0;
	for (size_t i = 0; i < plan.bindings.size(); ++i) {

		const auto& binding = plan.bindings[i];
		auto& param = params[i];

		// ログ出力
		Logger::Output(LogType::Engine, "ルートパラメータ[{}]: 名前={} 種別={}",
			i, binding.name, EnumAdapter<ShaderBindingKind>::ToString(binding.kind));

		param.ShaderVisibility = plan.visibilities[i];

		// ルート引数の種類に応じて構築
		switch (binding.parameterType) {
		case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {

			auto& range = ranges[tableIndex++];
			range.RangeType = ToRangeType(binding.kind);
			range.NumDescriptors = binding.bindCount == 0 ? UINT_MAX : binding.bindCount;
			range.BaseShaderRegister = binding.bindPoint;
			range.RegisterSpace = binding.space;
			range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
			range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
			if (binding.kind != ShaderBindingKind::Sampler) {
				range.Flags |= D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
			}
			param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			param.DescriptorTable.NumDescriptorRanges = 1;
			param.DescriptorTable.pDescriptorRanges = &range;
			break;
		}
		case D3D12_ROOT_PARAMETER_TYPE_CBV:
		case D3D12_ROOT_PARAMETER_TYPE_SRV:
		case D3D12_ROOT_PARAMETER_TYPE_UAV: {

			param.ParameterType = binding.parameterType;
			param.Descriptor.ShaderRegister = binding.bindPoint;
			param.Descriptor.RegisterSpace = binding.space;
			param.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
			break;
		}
		default:
			return {};
		}
	}

	// 所有配列を参照してシリアライズする
	D3D12_VERSIONED_ROOT_SIGNATURE_DESC signatureDesc{};
	signatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	signatureDesc.Desc_1_1.NumParameters = static_cast<UINT>(params.size());
	signatureDesc.Desc_1_1.pParameters = params.empty() ? nullptr : params.data();
	signatureDesc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(plan.staticSamplers.size());
	signatureDesc.Desc_1_1.pStaticSamplers = plan.staticSamplers.empty() ? nullptr : plan.staticSamplers.data();
	signatureDesc.Desc_1_1.Flags = plan.flags;
	ComPtr<ID3DBlob> signatureBlob;
	ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&signatureDesc, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {

		std::string diagnostics = errorBlob ? std::string(static_cast<const char*>(errorBlob->GetBufferPointer()),
			errorBlob->GetBufferSize()) : "診断情報がありません";
		if (!diagnostics.empty() && diagnostics.back() == '\0') {
			diagnostics.pop_back();
		}
		Logger::Output(LogType::Engine, spdlog::level::err, "[RootSignature] シリアライズに失敗しました: {}", diagnostics);
		return {};
	}
	// Device生成が成功してからBindingとともに公開する
	RootSignatureBuildResult result{};
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&result.rootSignature));
	if (FAILED(hr)) {
		Logger::Output(LogType::Engine, spdlog::level::err, "[RootSignature] 生成に失敗しました hr=0x{:08X}",
			static_cast<unsigned long>(hr));
		return {};
	}
	result.bindings = std::move(plan.bindings);
	return result;
}
