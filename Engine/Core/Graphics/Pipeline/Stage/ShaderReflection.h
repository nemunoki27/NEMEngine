#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// c++
#include <string>
#include <vector>
#include <cstdint>
// directX
#include <d3d12.h>
#include <dxcapi.h>

namespace Engine {

	//============================================================================
	//	ShaderReflection structures
	//============================================================================

	// シェーダ―の種類
	enum class ShaderStage :
		uint32_t {

		None = 0,
		VS = 1 << 0,
		AS = 1 << 1,
		MS = 1 << 2,
		GS = 1 << 3,
		PS = 1 << 4,
		CS = 1 << 5,
		Lib = 1 << 6,
	};
	 ShaderStage operator|(ShaderStage a, ShaderStage b);
	 ShaderStage& operator|=(ShaderStage& a, ShaderStage b);
	// シェーダーリソースの種類
	enum class ShaderBindingKind {

		CBV,
		SRV,
		UAV,
		Sampler,
		AccelStruct,
	};
	// シェーダーリソースのバインディング情報
	struct ShaderResourceBinding {

		// リソースの名前
		std::string name;
		// リソースの種類
		ShaderBindingKind kind;

		UINT bindPoint = 0;
		UINT bindCount = 1;
		UINT space = 0;
		ShaderStage stageMask = ShaderStage::None;
		D3D_SHADER_INPUT_TYPE rawType{};
	};
	// シェーダー入力セマンティクスの情報
	struct ShaderInputSemantic {

		// セマンティクスの名前
		std::string semanticName;
		UINT semanticIndex = 0;
		UINT registerIndex = 0;
		BYTE mask = 0;
		D3D_REGISTER_COMPONENT_TYPE componentType{};
	};
	// シェーダーのリフレクション情報
	struct ShaderReflectionInfo {

		// シェーダーリソースのバインディング情報
		std::vector<ShaderResourceBinding> resources;
		// シェーダー入力セマンティクスの情報
		std::vector<ShaderInputSemantic> inputs;
		uint64_t requiresFlags = 0;

		// コンピュートシェーダーのスレッドグループサイズ
		UINT threadGroupX = 1;
		UINT threadGroupY = 1;
		UINT threadGroupZ = 1;
	};
	// コンパイルされたシェーダーの情報
	struct CompiledShader {

		// シェーダーの種類
		ShaderStage stage{};
		// シェーダーのエントリーポイント
		std::wstring entry;
		// シェーダーのプロファイル
		std::wstring profile;

		// シェーダーのDXILバイナリ
		ComPtr<IDxcBlob> object;
		// シェーダーのリフレクション情報
		ShaderReflectionInfo reflection;
	};
} // Engine