#include "DxShaderCompiler.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Algorithm/Algorithm.h>

//============================================================================
//	DxShaderCompiler classMethods
//============================================================================

namespace {

	// D3D_SHADER_INPUT_TYPEからShaderBindingKindへの変換
	static ShaderBindingKind ToShaderBindingKind(D3D_SHADER_INPUT_TYPE type) {

		switch (type) {
			//============================================================================
			//	定数バッファ
			//============================================================================
		case D3D_SIT_CBUFFER:
			return ShaderBindingKind::CBV;
			//============================================================================
			//	SRV
			//============================================================================
		case D3D_SIT_TEXTURE:
		case D3D_SIT_TBUFFER:
		case D3D_SIT_STRUCTURED:
		case D3D_SIT_BYTEADDRESS:
			return ShaderBindingKind::SRV;
			//============================================================================
			//	UAV
			//============================================================================
		case D3D_SIT_UAV_RWTYPED:
		case D3D_SIT_UAV_RWSTRUCTURED:
		case D3D_SIT_UAV_RWBYTEADDRESS:
		case D3D_SIT_UAV_APPEND_STRUCTURED:
		case D3D_SIT_UAV_CONSUME_STRUCTURED:
		case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
			return ShaderBindingKind::UAV;
			//============================================================================
			//	サンプラー
			//============================================================================
		case D3D_SIT_SAMPLER:
			return ShaderBindingKind::Sampler;
			//============================================================================
			//	サンプラー
			//============================================================================
		case D3D_SIT_RTACCELERATIONSTRUCTURE:
			return ShaderBindingKind::AccelStruct;
		}
		// 未対応
		Assert::Call(false, "Unsupported D3D_SHADER_INPUT_TYPE");
		// デフォルトはCBVとする
		return ShaderBindingKind::CBV;
	}
	// シェーダーリフレクション情報のパース
	ShaderReflectionInfo ParseShaderReflection(ShaderStage stage, ID3D12ShaderReflection* reflection) {

		ShaderReflectionInfo out{};

		D3D12_SHADER_DESC shaderDesc{};
		reflection->GetDesc(&shaderDesc);
		// シェーダーリソースのバインディング情報を取得
		for (UINT i = 0; i < shaderDesc.BoundResources; ++i) {

			D3D12_SHADER_INPUT_BIND_DESC bindDesc{};
			reflection->GetResourceBindingDesc(i, &bindDesc);
			ShaderResourceBinding binding{};
			binding.name = bindDesc.Name ? bindDesc.Name : "";
			binding.kind = ToShaderBindingKind(bindDesc.Type);
			binding.bindPoint = bindDesc.BindPoint;
			binding.bindCount = bindDesc.BindCount;
			binding.space = bindDesc.Space;
			binding.stageMask = stage;
			binding.rawType = bindDesc.Type;

			// 取得したバインディング情報をリストに追加
			out.resources.emplace_back(std::move(binding));
		}

		// コンピュートシェーダーの場合、スレッドグループサイズを取得する
		if (Algorithm::HasFlag<ShaderStage>(stage, ShaderStage::CS)) {

			reflection->GetThreadGroupSize(&out.threadGroupX, &out.threadGroupY, &out.threadGroupZ);
		}
		// 頂点シェーダー
		if (Algorithm::HasFlag<ShaderStage>(stage, ShaderStage::VS)) {
			for (UINT i = 0; i < shaderDesc.InputParameters; ++i) {

				D3D12_SIGNATURE_PARAMETER_DESC parameterDesc{};
				reflection->GetInputParameterDesc(i, &parameterDesc);
				ShaderInputSemantic input{};
				input.semanticName = parameterDesc.SemanticName ? parameterDesc.SemanticName : "";
				input.semanticIndex = parameterDesc.SemanticIndex;
				input.registerIndex = parameterDesc.Register;
				input.mask = parameterDesc.Mask;
				input.componentType = parameterDesc.ComponentType;

				// 取得した入力セマンティクスの情報をリストに追加
				out.inputs.emplace_back(std::move(input));
			}
		}
		out.requiresFlags = reflection->GetRequiresFlags();
		return out;
	}
}

void DxShaderCompiler::Init() {

	dxcUtils_ = nullptr;
	dxcCompiler_ = nullptr;
	includeHandler_ = nullptr;

	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));

	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr));
}

CompiledShader DxShaderCompiler::CompileShader(const std::wstring& filePath,
	const wchar_t* profile, const wchar_t* entry, ShaderStage stage) {

	// 初期設定
	CompiledShader out{};
	out.stage = stage;
	out.entry = entry;
	out.profile = profile;

	ComPtr<IDxcBlobEncoding> source;
	HRESULT hr = dxcUtils_->LoadFile(filePath.c_str(), nullptr, &source);
	Assert::Call(SUCCEEDED(hr), "Failed to load HLSL file: " + Algorithm::ConvertString(filePath));
	// 読み込んだファイルの内容を設定する
	DxcBuffer srcBuf{};
	srcBuf.Ptr = source->GetBufferPointer();
	srcBuf.Size = source->GetBufferSize();
	// UTF8の文字コード
	srcBuf.Encoding = DXC_CP_UTF8;

	LPCWSTR args[] = {
		filePath.c_str(),
		L"-E", entry,
		L"-T", profile,
		L"-Zi", L"-Qembed_debug",
		L"-Zpr",
#if defined(_DEBUG)
		L"-Od",
#else
		L"-O3",
#endif
	};

	// コンパイル実行
	ComPtr<IDxcResult> result;
	hr = dxcCompiler_->Compile(&srcBuf, args, _countof(args),
		includeHandler_.Get(), IID_PPV_ARGS(&result));
	Assert::Call(SUCCEEDED(hr), "DXC invocation failed");
	HRESULT status = S_OK;
	result->GetStatus(&status);
	// コンパイルエラーの内容を取得
	ComPtr<IDxcBlobUtf8> errors;
	result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
	if (errors && errors->GetStringLength() > 0) {

		const char* msg = reinterpret_cast<const char*>(errors->GetBufferPointer());
		if (FAILED(status)) {

			Assert::Call(false, std::string("Shader compile failed: ") + msg);
		}
	}
	Assert::Call(SUCCEEDED(status), "Shader compile status failed");
	// コンパイルされたシェーダーオブジェクトを取得
	hr = result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&out.object), nullptr);
	Assert::Call(SUCCEEDED(hr), "Failed to get DXIL object");

	// シェーダーリフレクション情報を取得
	if (result->HasOutput(DXC_OUT_REFLECTION) && stage != ShaderStage::Lib) {

		// リフレクション情報を取得
		ComPtr<IDxcBlob> reflectionBlob;
		hr = result->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(&reflectionBlob), nullptr);
		Assert::Call(SUCCEEDED(hr), "Failed to get reflection blob");
		// リフレクション情報をパースするためのバッファを作成
		DxcBuffer reflectionBuffer{};
		reflectionBuffer.Ptr = reflectionBlob->GetBufferPointer();
		reflectionBuffer.Size = reflectionBlob->GetBufferSize();
		reflectionBuffer.Encoding = 0;
		// シェーダーリフレクションインターフェースを作成
		ComPtr<ID3D12ShaderReflection> shaderReflection;
		hr = dxcUtils_->CreateReflection(&reflectionBuffer, IID_PPV_ARGS(&shaderReflection));
		Assert::Call(SUCCEEDED(hr), "Failed to create shader reflection");
		out.reflection = ParseShaderReflection(stage, shaderReflection.Get());
	}
	return out;
}