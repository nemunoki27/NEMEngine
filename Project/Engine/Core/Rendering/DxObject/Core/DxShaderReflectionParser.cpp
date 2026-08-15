#include "DxShaderReflectionParser.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>

// directX
#include <d3d12shader.h>

namespace {

	// D3Dリソース種別をエンジン内のバインド種別へ変換する
	Engine::ShaderBindingKind ToShaderBindingKind(
		D3D_SHADER_INPUT_TYPE type) {

		switch (type) {
		case D3D_SIT_CBUFFER:
			return Engine::ShaderBindingKind::CBV;
		case D3D_SIT_TEXTURE:
		case D3D_SIT_TBUFFER:
		case D3D_SIT_STRUCTURED:
		case D3D_SIT_BYTEADDRESS:
			return Engine::ShaderBindingKind::SRV;
		case D3D_SIT_UAV_RWTYPED:
		case D3D_SIT_UAV_RWSTRUCTURED:
		case D3D_SIT_UAV_RWBYTEADDRESS:
		case D3D_SIT_UAV_APPEND_STRUCTURED:
		case D3D_SIT_UAV_CONSUME_STRUCTURED:
		case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
			return Engine::ShaderBindingKind::UAV;
		case D3D_SIT_SAMPLER:
			return Engine::ShaderBindingKind::Sampler;
		case D3D_SIT_RTACCELERATIONSTRUCTURE:
			return Engine::ShaderBindingKind::AccelStruct;
		default:
			return Engine::ShaderBindingKind::SRV;
		}
	}

	// HLSL型のスカラー成分数を取得する
	UINT GetDeclaredComponentCount(
		const D3D12_SHADER_TYPE_DESC& typeDesc) {

		UINT count = 1;
		switch (typeDesc.Class) {
		case D3D_SVC_VECTOR:
			count = (std::max)(1u, typeDesc.Columns);
			break;
		case D3D_SVC_MATRIX_ROWS:
		case D3D_SVC_MATRIX_COLUMNS:
			count = (std::max)(1u, typeDesc.Rows) *
				(std::max)(1u, typeDesc.Columns);
			break;
		default:
			break;
		}
		if (typeDesc.Elements > 0) {
			count *= typeDesc.Elements;
		}
		return (std::max)(1u, count);
	}

	// HLSLスカラー型のバイト数を取得する
	UINT GetDeclaredScalarByteSize(
		D3D_SHADER_VARIABLE_TYPE type) {

		switch (type) {
		case D3D_SVT_DOUBLE:
		case D3D_SVT_UINT64:
			return 8;
		default:
			return 4;
		}
	}

	// リソースバインド情報を取得する
	template <typename ReflectionT, typename DescT>
	void ParseResources(ReflectionT* reflection,
		const DescT& desc, Engine::ShaderStage stage,
		Engine::ShaderReflectionInfo& outReflection) {

		for (UINT index = 0; index < desc.BoundResources; ++index) {
			D3D12_SHADER_INPUT_BIND_DESC bindDesc{};
			if (FAILED(reflection->GetResourceBindingDesc(index, &bindDesc))) {
				continue;
			}
			Engine::ShaderResourceBinding binding{};
			binding.name = bindDesc.Name ? bindDesc.Name : "";
			binding.parameterID = Engine::MaterialParameterID::FromName(binding.name);
			binding.semantic = Engine::ResolveMaterialParameterSemantic(binding.name);
			binding.kind = ToShaderBindingKind(bindDesc.Type);
			binding.bindPoint = bindDesc.BindPoint;
			binding.bindCount = bindDesc.BindCount;
			binding.space = bindDesc.Space;
			binding.stageMask = stage;
			binding.rawType = bindDesc.Type;
			outReflection.resources.emplace_back(std::move(binding));
		}
	}

	// 定数バッファとStructuredBufferのレイアウトを取得する
	template <typename ReflectionT, typename DescT>
	void ParseBuffers(ReflectionT* reflection,
		const DescT& desc, Engine::ShaderReflectionInfo& outReflection) {

		for (UINT index = 0; index < desc.ConstantBuffers; ++index) {
			ID3D12ShaderReflectionConstantBuffer* constantBuffer =
				reflection->GetConstantBufferByIndex(index);
			if (!constantBuffer) {
				continue;
			}

			D3D12_SHADER_BUFFER_DESC bufferDesc{};
			if (FAILED(constantBuffer->GetDesc(&bufferDesc))) {
				continue;
			}
			Engine::ShaderConstantBufferInfo bufferInfo{};
			bufferInfo.name = bufferDesc.Name ? bufferDesc.Name : "";
			bufferInfo.size = bufferDesc.Size;
			const bool structured =
				bufferDesc.Type == D3D_CT_RESOURCE_BIND_INFO;
			for (const Engine::ShaderResourceBinding& resource :
				outReflection.resources) {

				const Engine::ShaderBindingKind expected = structured ?
					Engine::ShaderBindingKind::SRV : Engine::ShaderBindingKind::CBV;
				if (resource.kind == expected && resource.name == bufferInfo.name) {
					bufferInfo.bindPoint = resource.bindPoint;
					bufferInfo.space = resource.space;
					break;
				}
			}

			bufferInfo.variables.reserve(bufferDesc.Variables);
			for (UINT variableIndex = 0;
				variableIndex < bufferDesc.Variables; ++variableIndex) {

				ID3D12ShaderReflectionVariable* variable =
					constantBuffer->GetVariableByIndex(variableIndex);
				if (!variable) {
					continue;
				}
				D3D12_SHADER_VARIABLE_DESC variableDesc{};
				if (FAILED(variable->GetDesc(&variableDesc))) {
					continue;
				}
				ID3D12ShaderReflectionType* type = variable->GetType();
				D3D12_SHADER_TYPE_DESC typeDesc{};
				const bool hasType = type && SUCCEEDED(type->GetDesc(&typeDesc));
				if (hasType && typeDesc.Class == D3D_SVC_STRUCT &&
					typeDesc.Members > 0) {

					for (UINT memberIndex = 0;
						memberIndex < typeDesc.Members; ++memberIndex) {

						ID3D12ShaderReflectionType* memberType =
							type->GetMemberTypeByIndex(memberIndex);
						const char* memberName =
							type->GetMemberTypeName(memberIndex);
						if (!memberType || !memberName) {
							continue;
						}
						D3D12_SHADER_TYPE_DESC memberDesc{};
						if (FAILED(memberType->GetDesc(&memberDesc))) {
							continue;
						}
						Engine::ShaderConstantBufferVariable member{};
						member.name = memberName;
						member.parameterID =
							Engine::MaterialParameterID::FromName(member.name);
						member.semantic =
							Engine::ResolveMaterialParameterSemantic(member.name);
						member.offset = variableDesc.StartOffset + memberDesc.Offset;
						member.valueClass = memberDesc.Class;
						member.valueType = memberDesc.Type;
						member.rows = memberDesc.Rows;
						member.columns = memberDesc.Columns;
						member.elements = memberDesc.Elements;
						member.declaredComponentCount =
							GetDeclaredComponentCount(memberDesc);
						member.declaredByteSize = member.declaredComponentCount *
							GetDeclaredScalarByteSize(memberDesc.Type);
						member.size = member.declaredByteSize;
						bufferInfo.variables.emplace_back(std::move(member));
					}
					continue;
				}

				Engine::ShaderConstantBufferVariable info{};
				info.name = variableDesc.Name ? variableDesc.Name : "";
				info.parameterID = Engine::MaterialParameterID::FromName(info.name);
				info.semantic = Engine::ResolveMaterialParameterSemantic(info.name);
				info.offset = variableDesc.StartOffset;
				info.size = variableDesc.Size;
				info.used = (variableDesc.uFlags & D3D_SVF_USED) != 0;
				if (hasType) {
					info.valueClass = typeDesc.Class;
					info.valueType = typeDesc.Type;
					info.rows = typeDesc.Rows;
					info.columns = typeDesc.Columns;
					info.elements = typeDesc.Elements;
					info.declaredComponentCount =
						GetDeclaredComponentCount(typeDesc);
					info.declaredByteSize = info.declaredComponentCount *
						GetDeclaredScalarByteSize(typeDesc.Type);
				}
				if (!info.name.empty()) {
					bufferInfo.variables.emplace_back(std::move(info));
				}
			}

			if (bufferInfo.name.empty()) {
				continue;
			}
			if (structured) {
				Engine::ShaderStructuredBufferInfo structuredInfo{};
				structuredInfo.name = std::move(bufferInfo.name);
				structuredInfo.bindPoint = bufferInfo.bindPoint;
				structuredInfo.space = bufferInfo.space;
				structuredInfo.stride = bufferInfo.size;
				structuredInfo.variables = std::move(bufferInfo.variables);
				outReflection.structuredBuffers.emplace_back(
					std::move(structuredInfo));
			} else {
				outReflection.constantBuffers.emplace_back(std::move(bufferInfo));
			}
		}
	}

	// 通常シェーダーのリフレクションを取得する
	bool ParseShader(IDxcUtils* dxcUtils,
		const DxcBuffer& buffer, Engine::ShaderStage stage,
		Engine::ShaderReflectionInfo& outReflection) {

		ComPtr<ID3D12ShaderReflection> reflection;
		if (FAILED(dxcUtils->CreateReflection(
			&buffer, IID_PPV_ARGS(&reflection)))) {
			return false;
		}
		D3D12_SHADER_DESC desc{};
		if (FAILED(reflection->GetDesc(&desc))) {
			return false;
		}
		ParseResources(reflection.Get(), desc, stage, outReflection);
		ParseBuffers(reflection.Get(), desc, outReflection);
		if (Engine::Algorithm::HasFlag<Engine::ShaderStage>(
			stage, Engine::ShaderStage::CS)) {

			reflection->GetThreadGroupSize(&outReflection.threadGroupX,
				&outReflection.threadGroupY, &outReflection.threadGroupZ);
		}
		if (Engine::Algorithm::HasFlag<Engine::ShaderStage>(
			stage, Engine::ShaderStage::VS)) {

			for (UINT index = 0; index < desc.InputParameters; ++index) {
				D3D12_SIGNATURE_PARAMETER_DESC parameterDesc{};
				if (FAILED(reflection->GetInputParameterDesc(
					index, &parameterDesc))) {
					continue;
				}
				Engine::ShaderInputSemantic input{};
				input.semanticName = parameterDesc.SemanticName ?
					parameterDesc.SemanticName : "";
				input.semanticIndex = parameterDesc.SemanticIndex;
				input.registerIndex = parameterDesc.Register;
				input.mask = parameterDesc.Mask;
				input.componentType = parameterDesc.ComponentType;
				outReflection.inputs.emplace_back(std::move(input));
			}
		}
		outReflection.requiresFlags = reflection->GetRequiresFlags();
		return true;
	}

	// DXRライブラリ内の全エクスポートからリフレクションを統合する
	bool ParseLibrary(IDxcUtils* dxcUtils,
		const DxcBuffer& buffer,
		Engine::ShaderReflectionInfo& outReflection) {

		ComPtr<ID3D12LibraryReflection> reflection;
		if (FAILED(dxcUtils->CreateReflection(
			&buffer, IID_PPV_ARGS(&reflection)))) {
			return false;
		}
		D3D12_LIBRARY_DESC libraryDesc{};
		if (FAILED(reflection->GetDesc(&libraryDesc))) {
			return false;
		}
		for (UINT index = 0; index < libraryDesc.FunctionCount; ++index) {
			ID3D12FunctionReflection* function =
				reflection->GetFunctionByIndex(index);
			if (!function) {
				continue;
			}
			D3D12_FUNCTION_DESC functionDesc{};
			if (FAILED(function->GetDesc(&functionDesc))) {
				continue;
			}
			Engine::ShaderReflectionInfo functionReflection{};
			ParseResources(function, functionDesc,
				Engine::ShaderStage::Lib, functionReflection);
			ParseBuffers(function, functionDesc, functionReflection);
			functionReflection.requiresFlags =
				functionDesc.RequiredFeatureFlags;
			Engine::MergeShaderReflection(outReflection, functionReflection);
		}
		return true;
	}
}

bool Engine::ParseDxShaderReflection(
	IDxcUtils* dxcUtils, const DxcBuffer& reflectionBuffer,
	ShaderStage stage, ShaderReflectionInfo& outReflection) {

	outReflection = ShaderReflectionInfo{};
	if (!dxcUtils) {
		return false;
	}
	if (stage == ShaderStage::Lib) {
		return ParseLibrary(dxcUtils, reflectionBuffer, outReflection);
	}
	return ParseShader(dxcUtils, reflectionBuffer, stage, outReflection);
}
