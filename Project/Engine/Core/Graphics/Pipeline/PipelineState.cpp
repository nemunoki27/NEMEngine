#include "PipelineState.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>
#include <Engine/Logger/Logger.h>
#include <Engine/Utility/Algorithm/Algorithm.h>
#include <Engine/Utility/Enum/EnumAdapter.h>

//============================================================================
//	PipelineState classMethods
//============================================================================

namespace {

	// シェーダーのベースパス
	const std::filesystem::path kShaderBasePath = "./Engine/Assets/Shaders/";

	std::filesystem::path ResolveShaderPath(const std::string& file) {

		// Assets/Shaders/からの相対パスを優先
		std::filesystem::path direct = kShaderBasePath / file;
		if (std::filesystem::exists(direct)) {
			return direct;
		}

		// そのまま絶対/相対で存在するなら使う
		std::filesystem::path raw(file);
		if (std::filesystem::exists(raw)) {
			return raw;
		}

		// 最後に従来通りファイル名ベースで再帰検索
		std::filesystem::path searched{};
		if (Algorithm::FindFile(kShaderBasePath, std::filesystem::path(file).filename().string(), searched)) {
			return searched;
		}
		return {};
	}
	// シェーダーオブジェクトからD3D12_SHADER_BYTECODEを生成する
	D3D12_SHADER_BYTECODE ToBytecode(const CompiledShader* shader) {
		if (!shader || !shader->object) {
			return D3D12_SHADER_BYTECODE{};
		}

		D3D12_SHADER_BYTECODE byteCode{};
		byteCode.pShaderBytecode = shader->object->GetBufferPointer();
		byteCode.BytecodeLength = shader->object->GetBufferSize();
		return byteCode;
	}
	// フォーマットが RTV としてブレンド可能か
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
	// シェーダーのエントリーポイントを生成する
	std::wstring ResolveEntry(const std::string& entry) {

		return Algorithm::ConvertString(entry.empty() ? std::string("main") : entry);
	}
	// シェーダーのプロファイルを生成する
	std::wstring ResolveProfile(const std::string& profile, ShaderStage stage) {

		if (!profile.empty()) {
			return Algorithm::ConvertString(profile);
		}
		switch (stage) {
		case ShaderStage::VS: return L"vs_6_0";
		case ShaderStage::GS: return L"gs_6_0";
		case ShaderStage::PS: return L"ps_6_0";
		case ShaderStage::CS: return L"cs_6_0";
		case ShaderStage::MS: return L"ms_6_6";
		case ShaderStage::AS: return L"as_6_6";
		default:              return L"";
		}
	}
	// シェーダーをコンパイルし、CompiledShaderの配列に追加する
	void CompileOne(std::vector<CompiledShader>& shaders, DxShaderCompiler* compiler,
		const ShaderCompileDesc& desc, ShaderStage stage, const char* stageName) {

		if (desc.file.empty()) {
			return;
		}

		std::filesystem::path shaderPath = ResolveShaderPath(desc.file);
		Assert::Call(!shaderPath.empty(), std::string("Shader file not found: ") + desc.file);

		std::wstring entry = ResolveEntry(desc.entry);
		std::wstring profile = ResolveProfile(desc.profile, stage);

		shaders.emplace_back(compiler->CompileShader(shaderPath.wstring(), profile.c_str(), entry.c_str(), stage));
		Logger::Output(LogType::Engine, "Finished compiling {} for {}", stageName, shaderPath.string());
	}
	// シェーダーコンパイル
	std::vector<CompiledShader> Compile(DxShaderCompiler* compiler, const GraphicsPipelineDesc& desc) {

		// VS,GS,MSのいずれかとPSをコンパイルする
		std::vector<CompiledShader> shaders{};
		switch (desc.type) {
		case PipelineType::Vertex: {

			CompileOne(shaders, compiler, desc.preRaster, ShaderStage::VS, "VS");
			CompileOne(shaders, compiler, desc.pixel, ShaderStage::PS, "PS");
			break;
		}
		case PipelineType::Geometry: {

			CompileOne(shaders, compiler, desc.preRaster, ShaderStage::VS, "VS");
			CompileOne(shaders, compiler, desc.geometry, ShaderStage::GS, "GS");
			CompileOne(shaders, compiler, desc.pixel, ShaderStage::PS, "PS");
			break;
		}
		case PipelineType::Mesh: {

			CompileOne(shaders, compiler, desc.preRaster, ShaderStage::MS, "MS");
			CompileOne(shaders, compiler, desc.pixel, ShaderStage::PS, "PS");
			if (!desc.amplification.file.empty()) {
				CompileOne(shaders, compiler, desc.amplification, ShaderStage::AS, "AS");
			}
			break;
		}
		}
		return shaders;
	}
	// CompiledShaderの配列からconst CompiledShader*の配列を生成する
	std::vector<const CompiledShader*> MakeShaderPointers(const std::vector<CompiledShader>& shaders) {

		std::vector<const CompiledShader*> result{};
		result.reserve(shaders.size());
		for (const auto& shader : shaders) {
			result.emplace_back(&shader);
		}
		return result;
	}
}

const RootBindingLocation* PipelineState::FindBinding(
	ShaderBindingKind kind, UINT bindPoint, UINT space) const {

	auto found = registerBindingTable_.find(BindingRegisterKey{ kind, bindPoint, space });
	if (found == registerBindingTable_.end()) {
		return nullptr;
	}
	return &bindings_[found->second];
}

const RootBindingLocation* Engine::PipelineState::FindBindingByName(
	const std::string_view& name, ShaderBindingKind kind) const {

	const auto& table = nameBindingTables_[static_cast<size_t>(kind)];
	auto found = table.find(name);
	if (found == table.end()) {
		return nullptr;
	}
	return &bindings_[found->second];
}

ID3D12PipelineState* Engine::PipelineState::GetGraphicsPipeline(BlendMode blendMode) const {

	return graphicsPipelines_[static_cast<uint32_t>(blendMode)].Get();
}

bool Engine::PipelineState::CreateGraphics(ID3D12Device8* device, DxShaderCompiler* compiler, const GraphicsPipelineDesc& desc) {

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "Start CreateGraphicsPipeline: {} / {}", desc.preRaster.file, desc.pixel.file);

	// シェーダーのコンパイル
	const std::vector<CompiledShader> shaders = Compile(compiler, desc);
	const std::vector<const CompiledShader*> shaderPtrs = MakeShaderPointers(shaders);

	// ルートシグネイチャの自動生成
	AutoRootSignatureBuilder signatureBuilder{};
	RootSignatureBuildResult rootSignatureResult = signatureBuilder.Build(device, desc.type, shaderPtrs, desc.staticSamplers);
	// 結果を設定
	rootSignature_ = rootSignatureResult.rootSignature;
	bindings_ = std::move(rootSignatureResult.bindings);
	RebuildBindingLookupTables();

	switch (desc.type) {
	case PipelineType::Vertex:
	case PipelineType::Geometry:
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = rootSignature_.Get();
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
		Logger::Output(LogType::Engine, "Topology: {}", EnumAdapter<D3D12_PRIMITIVE_TOPOLOGY_TYPE>::ToString(desc.topologyType));
		Logger::Output(LogType::Engine, "DSVFormat: {}", EnumAdapter<DXGI_FORMAT>::ToString(desc.dsvFormat));

		// MRTのフォーマットを設定
		for (UINT i = 0; i < desc.numRenderTargets; ++i) {

			pipelineDesc.RTVFormats[i] = desc.rtvFormats[i];
			Logger::Output(LogType::Engine, "RTVFormats[{}]: {}", i, EnumAdapter<DXGI_FORMAT>::ToString(desc.rtvFormats[i]));
		}

		// 全てのBlendModeに対してパイプラインステートオブジェクトを生成
		for (const auto& mode : EnumAdapter<BlendMode>::GetEnumArray()) {

			// BlendModeとMRT数からブレンドステートを生成
			BlendMode blendMode = EnumAdapter<BlendMode>::FromString(mode).value_or(BlendMode::Normal);
			pipelineDesc.BlendState = MakeBlendDesc(device, blendMode, desc.rtvFormats, desc.numRenderTargets);

			// パイプラインステートオブジェクトの生成
			HRESULT hr = device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&graphicsPipelines_[static_cast<uint32_t>(blendMode)]));
			Assert::Call(SUCCEEDED(hr), "CreateGraphicsPipelineState failed");
		}
		break;
	}
	case PipelineType::Mesh:
	{
		D3DX12_MESH_SHADER_PIPELINE_STATE_DESC pipelineDesc{};
		pipelineDesc.pRootSignature = rootSignature_.Get();
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
		Logger::Output(LogType::Engine, "DSVFormat: {}", EnumAdapter<DXGI_FORMAT>::ToString(desc.dsvFormat));

		// MRTのフォーマットを設定
		for (UINT i = 0; i < desc.numRenderTargets; ++i) {

			pipelineDesc.RTVFormats[i] = desc.rtvFormats[i];
			Logger::Output(LogType::Engine, "RTVFormats[{}]: {}", i, EnumAdapter<DXGI_FORMAT>::ToString(desc.rtvFormats[i]));
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
			HRESULT hr = device->CreatePipelineState(&streamDesc, IID_PPV_ARGS(&graphicsPipelines_[static_cast<uint32_t>(blendMode)]));
			Assert::Call(SUCCEEDED(hr), "CreatePipelineState failed");
		}
		break;
	}
	}
	Logger::Output(LogType::Engine, "Created GraphicsPipeline");
	Logger::EndSection(LogType::Engine);
	return true;
}

bool Engine::PipelineState::CreateCompute(ID3D12Device8* device, DxShaderCompiler* compiler, const ComputePipelineDesc& desc) {

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "Start CreateComputePipeline: {}", desc.compute.file);

	// シェーダーのコンパイル
	const std::filesystem::path shaderPath = ResolveShaderPath(desc.compute.file);
	Assert::Call(!shaderPath.empty(), std::string("Compute shader file not found: ") + desc.compute.file);
	const std::wstring entry = ResolveEntry(desc.compute.entry);
	const std::wstring profile = ResolveProfile(desc.compute.profile, ShaderStage::CS);
	CompiledShader shader = compiler->CompileShader(shaderPath.wstring(), profile.c_str(), entry.c_str(), ShaderStage::CS);
	Logger::Output(LogType::Engine, "Finished compiling CS for {}", shaderPath.string());

	// スレッドサイズを設定
	threadGroupX_ = shader.reflection.threadGroupX;
	threadGroupY_ = shader.reflection.threadGroupY;
	threadGroupZ_ = shader.reflection.threadGroupZ;

	Logger::Output(LogType::Engine, "ThreadGroup[{},{},{}]", threadGroupX_, threadGroupY_, threadGroupZ_);

	// ルートシグネイチャの自動生成
	AutoRootSignatureBuilder builder;
	auto rootSignatureResult = builder.Build(device, PipelineType::Compute, { &shader }, desc.staticSamplers);
	// 結果を設定
	rootSignature_ = rootSignatureResult.rootSignature;
	bindings_ = std::move(rootSignatureResult.bindings);
	RebuildBindingLookupTables();

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = rootSignature_.Get();
	pipelineDesc.CS = ToBytecode(&shader);

	// パイプラインステートオブジェクトの生成
	HRESULT hr = device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&computePipeline_));
	Assert::Call(SUCCEEDED(hr), "CreatePipelineState failed");
	Logger::Output(LogType::Engine, "Created ComputePipeline");
	Logger::EndSection(LogType::Engine);
	return true;
}

void Engine::PipelineState::RebuildBindingLookupTables() {

	registerBindingTable_.clear();
	for (auto& table : nameBindingTables_) {
		table.clear();
	}
	registerBindingTable_.reserve(bindings_.size());
	for (auto& table : nameBindingTables_) {

		table.reserve(bindings_.size());
	}
	for (size_t i = 0; i < bindings_.size(); ++i) {

		const auto& bind = bindings_[i];
		// register/space/kindから引くためのテーブルと、名前から引くためのテーブルの両方に登録する
		registerBindingTable_[BindingRegisterKey{ bind.kind, bind.bindPoint, bind.space }] = i;
		nameBindingTables_[static_cast<size_t>(bind.kind)][bind.name] = i;
	}
}