#include "PipelineStateBuilder.h"

//============================================================================
//	include
//============================================================================
#include "PipelineShaderLoader.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

using namespace Engine;
using namespace Engine::PipelineShaderLoader;

namespace {

	// シェーダーオブジェクトからD3D12_SHADER_BYTECODEを生成する
	D3D12_SHADER_BYTECODE ToBytecode(const CompiledShader* shader) {
		if (!shader || !shader->IsValid()) {
			return D3D12_SHADER_BYTECODE{};
		}

		D3D12_SHADER_BYTECODE byteCode{};
		byteCode.pShaderBytecode = shader->GetBytecodePointer();
		byteCode.BytecodeLength = shader->GetBytecodeSize();
		return byteCode;
	}
	// フォーマットがRTVとしてブレンド可能か
	bool IsBlendableRenderTargetFormat(ID3D12Device* device, DXGI_FORMAT format) {

		D3D12_FEATURE_DATA_FORMAT_SUPPORT support{};
		support.Format = format;
		device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &support, sizeof(support));

		// チェックに失敗した場合はブレンド不可とする
		bool isRenderTarget = (support.Support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) != 0;
		bool isBlendable = (support.Support1 & D3D12_FORMAT_SUPPORT1_BLENDABLE) != 0;
		return isRenderTarget && isBlendable;
	}
	// BlendModeとMRT数からD3D12_BLEND_DESCを生成する
	D3D12_BLEND_DESC MakeBlendDesc(ID3D12Device* device, BlendMode mode,
		const DXGI_FORMAT* rtvFormats, UINT mrtCount) {

		D3D12_BLEND_DESC desc{};
		desc.IndependentBlendEnable = (1 < mrtCount) ? TRUE : FALSE;

		// 指定モードでブレンド作成
		D3D12_RENDER_TARGET_BLEND_DESC blendDesc{};
		BlendState blendState;
		blendState.Create(mode, blendDesc);
		for (UINT i = 0; i < mrtCount; ++i) {

			// MRTのフォーマットがブレンド可能かチェック
			if (IsBlendableRenderTargetFormat(device, rtvFormats[i])) {

				desc.RenderTarget[i] = blendDesc;
			} else {

				// 非カラー出力はブレンド無効
				desc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
				desc.RenderTarget[i].BlendEnable = FALSE;
			}
		}
		return desc;
	}
	// CompiledShaderの配列からconst CompiledShader*の配列を生成する

}

std::unique_ptr<Engine::PipelineState> Engine::PipelineStateBuilder::CreateGraphics(GraphicsResourceRetirement& retirement, ID3D12Device8* device,
	DxShaderCompiler* compiler, const GraphicsPipelineDesc& desc, const ShaderAsset* metadata) {

	auto state = std::make_unique<PipelineState>();
	if (!BuildGraphics(*state, device, compiler, desc)) {
		return nullptr;
	}
	if (metadata) {
		ApplyShaderParameterMetadata(state->graphicsReflection_, *metadata);
		ApplyShaderParameterMetadata(state->computeReflection_, *metadata);
	}
	// 完成したPSOへ回収先を接続してから公開する
	state->SetRetirementQueue(retirement);
	return state;
}

std::unique_ptr<Engine::PipelineState> Engine::PipelineStateBuilder::CreateCompute(GraphicsResourceRetirement& retirement, ID3D12Device8* device,
	DxShaderCompiler* compiler, const ComputePipelineDesc& desc, const ShaderAsset* metadata) {

	auto state = std::make_unique<PipelineState>();
	if (!BuildCompute(*state, device, compiler, desc)) {
		return nullptr;
	}
	if (metadata) {
		ApplyShaderParameterMetadata(state->graphicsReflection_, *metadata);
		ApplyShaderParameterMetadata(state->computeReflection_, *metadata);
	}
	// 完成したPSOへ回収先を接続してから公開する
	state->SetRetirementQueue(retirement);
	return state;
}

bool Engine::PipelineStateBuilder::BuildGraphics(PipelineState& state, ID3D12Device8* device, DxShaderCompiler* compiler, const GraphicsPipelineDesc& desc) {

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "GraphicsPipelineの作成を開始します {} / {}",
		desc.preRaster.file, desc.pixel.file);

	// シェーダーのコンパイル
	GraphicsCompileResult compileResult = Compile(compiler, desc);
	if (!compileResult.success) {
		Logger::Output(LogType::Engine, "GraphicsPipeline用Shaderのコンパイルに失敗しました");
		Logger::EndSection(LogType::Engine);
		return false;
	}
	const std::vector<CompiledShader>& shaders = compileResult.shaders;
	const std::vector<const CompiledShader*> shaderPtrs = MakeShaderPointers(shaders);

	// 全ステージのリフレクションを統合して名前指定のサンプラー設定を解決する
	state.graphicsReflection_ = ShaderReflectionInfo{};
	for (const auto& shader : shaders) {
		Engine::MergeShaderReflection(state.graphicsReflection_, shader.reflection);
	}
	const std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers =
		BuildPipelineStaticSamplers(state.graphicsReflection_, desc.staticSamplers,
			desc.staticSamplerOverrides);

	// ルートシグネイチャの自動生成
	AutoRootSignatureBuilder signatureBuilder{};
	RootSignatureBuildResult rootSignatureResult = signatureBuilder.Build(
		device, desc.type, shaderPtrs, staticSamplers);
	// 結果を設定
	state.rootSignature_ = rootSignatureResult.rootSignature;
	state.bindings_ = std::move(rootSignatureResult.bindings);
	state.RebuildBindingLookupTables();

	// PSO生成の成否、失敗したBlendModeがあればfalseを返す
	bool success = true;

	switch (desc.type) {
	case PipelineType::Vertex:
	case PipelineType::Geometry:
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = state.rootSignature_.Get();
		AutoInputLayoutBuilder inputBuilder{};
		InputLayoutBuildResult inputLayoutResult{};
		for (const auto& shader : shaders) {
			switch (shader.stage) {
			case ShaderStage::VS:
				pipelineDesc.VS = ToBytecode(&shader);
				inputLayoutResult = inputBuilder.Build(shader);
				break;
			case ShaderStage::GS:
				pipelineDesc.GS = ToBytecode(&shader);
				break;
			case ShaderStage::PS:
				pipelineDesc.PS = ToBytecode(&shader);
				break;
			}
		}
		pipelineDesc.InputLayout = inputLayoutResult.GetDesc();
		pipelineDesc.RasterizerState = desc.rasterizer;
		pipelineDesc.DepthStencilState = desc.depthStencil;
		pipelineDesc.NumRenderTargets = desc.numRenderTargets;
		pipelineDesc.SampleDesc = desc.sampleDesc;
		pipelineDesc.SampleMask = UINT_MAX;
		pipelineDesc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
		pipelineDesc.PrimitiveTopologyType = desc.topologyType;
		pipelineDesc.DSVFormat = desc.dsvFormat;
		Logger::Output(LogType::Engine, "トポロジ: {}",
			EnumAdapter<D3D12_PRIMITIVE_TOPOLOGY_TYPE>::ToString(desc.topologyType));
		Logger::Output(LogType::Engine, "DSV形式: {}", EnumAdapter<DXGI_FORMAT>::ToString(desc.dsvFormat));

		// MRTのフォーマットを設定
		for (UINT i = 0; i < desc.numRenderTargets; ++i) {

			pipelineDesc.RTVFormats[i] = desc.rtvFormats[i];
			Logger::Output(LogType::Engine, "RTV形式[{}]: {}", i,
				EnumAdapter<DXGI_FORMAT>::ToString(desc.rtvFormats[i]));
		}

		// 全てのBlendModeに対してパイプラインステートオブジェクトを生成
		for (const auto& mode : EnumAdapter<BlendMode>::GetEnumArray()) {

			// BlendModeとMRT数からブレンドステートを生成
			BlendMode blendMode = EnumAdapter<BlendMode>::FromString(mode).value_or(BlendMode::Normal);
			pipelineDesc.BlendState = MakeBlendDesc(device, blendMode, desc.rtvFormats, desc.numRenderTargets);

			// パイプラインステートオブジェクトの生成
			HRESULT hr = device->CreateGraphicsPipelineState(
				&pipelineDesc,
				IID_PPV_ARGS(&state.graphicsPipelines_[static_cast<uint32_t>(blendMode)]));
			if (FAILED(hr)) {
				Logger::Output(LogType::Engine,
					"GraphicsPipelineStateの作成に失敗しました path={} blend={}", desc.pixel.file, mode);
				success = false;
				continue;
			}

			const std::string psoName =
				Algorithm::PathToUTF8(Algorithm::PathFromUTF8(desc.preRaster.file).stem()) +
				"|" + Algorithm::PathToUTF8(Algorithm::PathFromUTF8(desc.pixel.file).stem()) +
				"[" + mode + "]";
			state.graphicsPipelines_[static_cast<uint32_t>(blendMode)]->SetName(Algorithm::ConvertString(psoName).c_str());
		}
		break;
	}
	case PipelineType::Mesh:
	{
		D3DX12_MESH_SHADER_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = state.rootSignature_.Get();
		for (const auto& shader : shaders) {
			switch (shader.stage) {
			case ShaderStage::MS:
				pipelineDesc.MS = ToBytecode(&shader);
				break;
			case ShaderStage::PS:
				pipelineDesc.PS = ToBytecode(&shader);
				break;
			case ShaderStage::AS:
				pipelineDesc.AS = ToBytecode(&shader);
				break;
			}
		}
		pipelineDesc.RasterizerState = desc.rasterizer;
		pipelineDesc.DepthStencilState = desc.depthStencil;
		pipelineDesc.PrimitiveTopologyType = desc.topologyType;
		pipelineDesc.NumRenderTargets = desc.numRenderTargets;
		pipelineDesc.SampleDesc = desc.sampleDesc;
		pipelineDesc.SampleMask = UINT_MAX;
		pipelineDesc.DSVFormat = desc.dsvFormat;
		Logger::Output(LogType::Engine, "DSV形式: {}", EnumAdapter<DXGI_FORMAT>::ToString(desc.dsvFormat));

		// MRTのフォーマットを設定
		for (UINT i = 0; i < desc.numRenderTargets; ++i) {

			pipelineDesc.RTVFormats[i] = desc.rtvFormats[i];
			Logger::Output(LogType::Engine, "RTV形式[{}]: {}", i,
				EnumAdapter<DXGI_FORMAT>::ToString(desc.rtvFormats[i]));
		}

		// 全てのBlendModeに対してパイプラインステートオブジェクトを生成
		for (const auto& mode : EnumAdapter<BlendMode>::GetEnumArray()) {

			// BlendModeとMRT数からブレンドステートを生成
			BlendMode blendMode = EnumAdapter<BlendMode>::FromString(mode).value_or(BlendMode::Normal);
			pipelineDesc.BlendState = MakeBlendDesc(device, blendMode, desc.rtvFormats, desc.numRenderTargets);

			// ストリームの生成
			CD3DX12_PIPELINE_MESH_STATE_STREAM pipelineStream = CD3DX12_PIPELINE_MESH_STATE_STREAM(pipelineDesc);
			D3D12_PIPELINE_STATE_STREAM_DESC streamDesc{};
			streamDesc.pPipelineStateSubobjectStream = &pipelineStream;
			streamDesc.SizeInBytes = sizeof(pipelineStream);

			// パイプラインステートオブジェクトの生成
			HRESULT hr = device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&state.graphicsPipelines_[static_cast<uint32_t>(blendMode)]));
			if (FAILED(hr)) {
				Logger::Output(LogType::Engine,
					"PipelineStateの作成に失敗しました path={} blend={}", desc.pixel.file, mode);
				success = false;
				continue;
			}

			const std::string psoName =
				Algorithm::PathToUTF8(Algorithm::PathFromUTF8(desc.preRaster.file).stem()) +
				"|" + Algorithm::PathToUTF8(Algorithm::PathFromUTF8(desc.pixel.file).stem()) +
				"[" + mode + "]";
			state.graphicsPipelines_[static_cast<uint32_t>(blendMode)]->SetName(Algorithm::ConvertString(psoName).c_str());
		}
		break;
	}
	}
	Logger::Output(LogType::Engine, success ?
		"GraphicsPipelineを作成しました" : "GraphicsPipelineの作成に失敗しました");
	Logger::EndSection(LogType::Engine);
	return success;
}

bool Engine::PipelineStateBuilder::BuildCompute(PipelineState& state, ID3D12Device8* device, DxShaderCompiler* compiler, const ComputePipelineDesc& desc) {

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "ComputePipelineの作成を開始します path={}", desc.compute.file);

	// Graphicsと同じCook経路でCSを取得する
	std::vector<CompiledShader> shaders;
	if (!CompileOne(shaders, compiler, desc.compute,
		ShaderStage::CS, "CS") || shaders.empty()) {
		Logger::Output(LogType::Engine,
			"[PostProcess] Shaderの読み込みに失敗しました path={}", desc.compute.file);
		Logger::EndSection(LogType::Engine);
		return false;
	}
	CompiledShader& shader = shaders.front();
	Logger::Output(LogType::Engine, "CSのコンパイルが完了しました path={}",
		desc.compute.file);

	// スレッドサイズを設定
	state.threadGroupX_ = shader.reflection.threadGroupX;
	state.threadGroupY_ = shader.reflection.threadGroupY;
	state.threadGroupZ_ = shader.reflection.threadGroupZ;
	state.computeReflection_ = shader.reflection;

	Logger::Output(LogType::Engine, "スレッドグループ[{},{},{}]", state.threadGroupX_, state.threadGroupY_, state.threadGroupZ_);

	// ルートシグネイチャの自動生成
	AutoRootSignatureBuilder builder;
	const std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers =
		BuildPipelineStaticSamplers(shader.reflection, desc.staticSamplers,
			desc.staticSamplerOverrides);
	auto rootSignatureResult = builder.Build(device, PipelineType::Compute, { &shader }, staticSamplers);
	// 結果を設定
	state.rootSignature_ = rootSignatureResult.rootSignature;
	state.bindings_ = std::move(rootSignatureResult.bindings);
	state.RebuildBindingLookupTables();

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = state.rootSignature_.Get();
	pipelineDesc.CS = ToBytecode(&shader);

	// パイプラインステートオブジェクトの生成
	HRESULT hr = device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&state.computePipeline_));
	if (FAILED(hr)) {
		Logger::Output(LogType::Engine,
			"[PostProcess] ComputePipelineStateの作成に失敗しました path={}", desc.compute.file);
		Logger::EndSection(LogType::Engine);
		return false;
	}
	state.computePipeline_->SetName(Algorithm::ConvertString(Algorithm::PathToUTF8(
		Algorithm::PathFromUTF8(desc.compute.file).stem())).c_str());
	Logger::Output(LogType::Engine, "ComputePipelineを作成しました");
	Logger::EndSection(LogType::Engine);
	return true;
}
