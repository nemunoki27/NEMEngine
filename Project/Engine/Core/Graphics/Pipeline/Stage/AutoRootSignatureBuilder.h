#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/Stage/ShaderReflection.h>

namespace Engine {

	//============================================================================
	//	AutoRootSignatureBuilder structures
	//============================================================================

	// パイプラインの種類
	enum class PipelineType {

		Vertex,
		Geometry,
		Mesh,
		Compute,
		Raytracing,
	};
	// 生成後にのルート引き数の配置情報
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
	// ルートシグネチャの生成結果
	struct RootSignatureBuildResult {

		ComPtr<ID3D12RootSignature> rootSignature;
		std::vector<RootBindingLocation> bindings;
	};

	//============================================================================
	//	AutoRootSignatureBuilder class
	//	自動でルートシグネチャを生成するクラス
	//============================================================================
	class AutoRootSignatureBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AutoRootSignatureBuilder() = default;
		~AutoRootSignatureBuilder() = default;

		// 指定されたシェーダー群から必要なルート引数を自動で推測し、ルートシグネチャを生成する
		RootSignatureBuildResult Build(ID3D12Device* device, PipelineType pipelineType,
			const std::vector<const CompiledShader*>& shaders, const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers = {});
	};
} // Engine