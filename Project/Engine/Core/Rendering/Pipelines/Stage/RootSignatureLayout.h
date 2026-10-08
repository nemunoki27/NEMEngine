#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	RootSignatureLayout structures
	//============================================================================
	// パイプラインの種類
	enum class PipelineType {

		Vertex,
		Geometry,
		Mesh,
		Compute,
		Raytracing,
	};
	// 生成後のルート引数の配置情報
	struct RootBindingLocation {

		// リソースの名前
		std::string name;
		// リソースの種類
		ShaderBindingKind kind = ShaderBindingKind::CBV;
		// ルート引数のインデックス
		UINT rootParameterIndex = 0;
		UINT bindPoint = 0;
		UINT bindCount = 1;
		UINT space = 0;
		ShaderStage stageMask = ShaderStage::None;

		// ルート引数の種類
		D3D_SHADER_INPUT_TYPE rawType{};
		D3D12_ROOT_PARAMETER_TYPE parameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	};
	// GPU生成に渡すルート配置と静的Sampler
	struct RootSignaturePlan {

		std::vector<RootBindingLocation> bindings;
		std::vector<D3D12_SHADER_VISIBILITY> visibilities;
		std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers;
		D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		// 失敗時は部分配置を公開しない
		std::string diagnostics;

		bool IsValid() const { return diagnostics.empty(); }
	};
}
