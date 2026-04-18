#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Pipeline/Stage/ShaderReflection.h>

// directX
#include <d3d12shader.h>

namespace Engine {

	//============================================================================
	//	DxShaderCompiler class
	//	DXCでHLSLをDXILにコンパイルし、必要なシェーダBlob群を生成する。
	//============================================================================
	class DxShaderCompiler {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DxShaderCompiler() = default;
		~DxShaderCompiler() = default;

		// DXCの初期化(コンパイラインターフェース/インクルードハンドラ等の準備)
		void Init();

		// シェーダーコンパイル
		CompiledShader CompileShader(const std::wstring& filePath,
			const wchar_t* profile, const wchar_t* entry, ShaderStage stage);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ComPtr<IDxcUtils> dxcUtils_;
		ComPtr<IDxcCompiler3> dxcCompiler_;
		ComPtr<IDxcIncludeHandler> includeHandler_;
	};
}; // Engine