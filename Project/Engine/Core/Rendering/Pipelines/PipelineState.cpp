#include "PipelineState.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>

// c++
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_map>

//============================================================================
//	PipelineState classMethods
//============================================================================

uint64_t Engine::PipelineState::NextUniqueID() {

	// 生成のたびに増える、0は未設定を表すため1から始める
	static std::atomic<uint64_t> counter{ 0 };
	return ++counter;
}

void Engine::PipelineState::ApplyShaderMetadata(
	const ShaderAsset& asset) {

	ApplyShaderParameterMetadata(
		graphicsReflection_, asset);
	ApplyShaderParameterMetadata(
		computeReflection_, asset);
}

namespace {

	// シェーダーソース参照(GUIDまたはパス)を実体パスへ解決する
	std::filesystem::path ResolveShaderPath(const std::string& file) {

		return ShaderSourcePath::Resolve(file);
	}
	// あるステージのリフレクションを統合先へマージする、定数バッファは名前、リソースはregister/space/kindで重複排除する
	void MergeShaderReflection(ShaderReflectionInfo& dst, const ShaderReflectionInfo& src) {

		for (const ShaderConstantBufferInfo& cb : src.constantBuffers) {

			bool exists = false;
			for (const ShaderConstantBufferInfo& existing : dst.constantBuffers) {
				if (existing.name == cb.name) {
					exists = true;
					break;
				}
			}
			if (!exists) {
				dst.constantBuffers.push_back(cb);
			}
		}
		for (const ShaderStructuredBufferInfo& buffer : src.structuredBuffers) {

			bool exists = false;
			for (const ShaderStructuredBufferInfo& existing : dst.structuredBuffers) {
				if (existing.name == buffer.name) {
					exists = true;
					break;
				}
			}
			if (!exists) {
				dst.structuredBuffers.push_back(buffer);
			}
		}
		for (const ShaderResourceBinding& res : src.resources) {

			bool exists = false;
			for (ShaderResourceBinding& existing : dst.resources) {
				if (existing.kind == res.kind && existing.bindPoint == res.bindPoint && existing.space == res.space) {

					existing.stageMask |= res.stageMask;
					exists = true;
					break;
				}
			}
			if (!exists) {
				dst.resources.push_back(res);
			}
		}
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
	// グラフィックスシェーダーのコンパイル結果
	struct GraphicsCompileResult {

		std::vector<CompiledShader> shaders;
		bool success = true;
	};
	// シェーダーをコンパイルし、CompiledShaderの配列に追加する
	bool CompileOne(std::vector<CompiledShader>& shaders, DxShaderCompiler* compiler,
		const ShaderCompileDesc& desc, ShaderStage stage, const char* stageName) {

		if (desc.file.empty()) {
			return true;
		}

		std::filesystem::path shaderPath = ResolveShaderPath(desc.file);
		if (shaderPath.empty()) {
			Logger::Output(LogType::Engine, "Shader file not found: {}", desc.file);
			return false;
		}

		std::wstring entry = ResolveEntry(desc.entry);
		std::wstring profile = ResolveProfile(desc.profile, stage);

		CompiledShader shader = compiler->CompileShader(shaderPath.wstring(), profile.c_str(), entry.c_str(), stage);
		if (!shader.object) {
			Logger::Output(LogType::Engine, "Failed compiling {} for {}",
				stageName, Algorithm::PathToUTF8(shaderPath));
			return false;
		}
		shaders.emplace_back(std::move(shader));
		Logger::Output(LogType::Engine, "Finished compiling {} for {}",
			stageName, Algorithm::PathToUTF8(shaderPath));
		return true;
	}
	// シェーダーコンパイル
	GraphicsCompileResult Compile(DxShaderCompiler* compiler, const GraphicsPipelineDesc& desc) {

		// VS,GS,MSのいずれかとPSをコンパイルする
		GraphicsCompileResult result{};
		switch (desc.type) {
		case PipelineType::Vertex: {

			result.success &= CompileOne(result.shaders, compiler, desc.preRaster, ShaderStage::VS, "VS");
			result.success &= CompileOne(result.shaders, compiler, desc.pixel, ShaderStage::PS, "PS");
			break;
		}
		case PipelineType::Geometry: {

			result.success &= CompileOne(result.shaders, compiler, desc.preRaster, ShaderStage::VS, "VS");
			result.success &= CompileOne(result.shaders, compiler, desc.geometry, ShaderStage::GS, "GS");
			result.success &= CompileOne(result.shaders, compiler, desc.pixel, ShaderStage::PS, "PS");
			break;
		}
		case PipelineType::Mesh: {

			result.success &= CompileOne(result.shaders, compiler, desc.preRaster, ShaderStage::MS, "MS");
			result.success &= CompileOne(result.shaders, compiler, desc.pixel, ShaderStage::PS, "PS");
			if (!desc.amplification.file.empty()) {
				result.success &= CompileOne(result.shaders, compiler, desc.amplification, ShaderStage::AS, "AS");
			}
			break;
		}
		}
		return result;
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

	bool IsSameStaticSamplerSlot(const D3D12_STATIC_SAMPLER_DESC& sampler, UINT shaderRegister, UINT registerSpace) {

		return sampler.ShaderRegister == shaderRegister && sampler.RegisterSpace == registerSpace;
	}

	D3D12_STATIC_SAMPLER_DESC MakeStaticSamplerDesc(
		const PipelineStaticSamplerSettings& settings, UINT shaderRegister, UINT registerSpace) {

		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter = settings.filter;
		sampler.AddressU = settings.addressU;
		sampler.AddressV = settings.addressV;
		sampler.AddressW = settings.addressW;
		sampler.MipLODBias = settings.mipLODBias;
		sampler.MaxAnisotropy = (std::clamp)(settings.maxAnisotropy, 1u, 16u);
		sampler.ComparisonFunc = settings.comparisonFunc;
		sampler.BorderColor = settings.borderColor;
		sampler.MinLOD = settings.minLOD;
		sampler.MaxLOD = settings.maxLOD;
		sampler.ShaderRegister = shaderRegister;
		sampler.RegisterSpace = registerSpace;
		sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		return sampler;
	}

	void ReplaceStaticSampler(std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers,
		const D3D12_STATIC_SAMPLER_DESC& sampler) {

		auto found = std::find_if(staticSamplers.begin(), staticSamplers.end(), [&](const D3D12_STATIC_SAMPLER_DESC& existing) {
			return IsSameStaticSamplerSlot(existing, sampler.ShaderRegister, sampler.RegisterSpace);
			});
		if (found != staticSamplers.end()) {
			*found = sampler;
			return;
		}
		staticSamplers.emplace_back(sampler);
	}

	bool HasStaticSamplerSlot(const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers,
		UINT shaderRegister, UINT registerSpace) {

		return std::any_of(staticSamplers.begin(), staticSamplers.end(), [&](const D3D12_STATIC_SAMPLER_DESC& sampler) {
			return IsSameStaticSamplerSlot(sampler, shaderRegister, registerSpace);
			});
	}

	std::vector<D3D12_STATIC_SAMPLER_DESC> BuildComputeStaticSamplers(
		const ShaderReflectionInfo& reflection, const std::vector<D3D12_STATIC_SAMPLER_DESC>& baseSamplers,
		const PipelineStaticSamplerOverrideSet& overrides) {

		std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers = baseSamplers;
		for (const ShaderResourceBinding& binding : reflection.resources) {

			if (binding.kind != ShaderBindingKind::Sampler) {
				continue;
			}

			const auto overrideIt = overrides.byName.find(binding.name);
			const bool hasOverride = overrideIt != overrides.byName.end();
			if (!hasOverride && !overrides.fillMissingSamplers) {
				continue;
			}

			const PipelineStaticSamplerSettings settings =
				hasOverride ? overrideIt->second : PipelineStaticSamplerSettings{};
			const UINT count = (std::max)(1u, binding.bindCount);
			for (UINT i = 0; i < count; ++i) {

				const UINT shaderRegister = binding.bindPoint + i;
				if (!hasOverride && HasStaticSamplerSlot(staticSamplers, shaderRegister, binding.space)) {
					continue;
				}
				ReplaceStaticSampler(staticSamplers, MakeStaticSamplerDesc(settings, shaderRegister, binding.space));
			}
		}
		return staticSamplers;
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
	GraphicsCompileResult compileResult = Compile(compiler, desc);
	if (!compileResult.success) {
		Logger::Output(LogType::Engine, "Failed GraphicsPipeline shader compilation");
		Logger::EndSection(LogType::Engine);
		return false;
	}
	const std::vector<CompiledShader>& shaders = compileResult.shaders;
	const std::vector<const CompiledShader*> shaderPtrs = MakeShaderPointers(shaders);

	// ルートシグネイチャの自動生成
	AutoRootSignatureBuilder signatureBuilder{};
	RootSignatureBuildResult rootSignatureResult = signatureBuilder.Build(device, desc.type, shaderPtrs, desc.staticSamplers);
	// 結果を設定
	rootSignature_ = rootSignatureResult.rootSignature;
	bindings_ = std::move(rootSignatureResult.bindings);
	RebuildBindingLookupTables();

	// 全ステージのリフレクションを統合する、MaterialParameters cbufferやテクスチャSRVを後段で解決するため
	graphicsReflection_ = ShaderReflectionInfo{};
	for (const auto& shader : shaders) {
		MergeShaderReflection(graphicsReflection_, shader.reflection);
	}

	// PSO生成の成否、失敗したBlendModeがあればfalseを返す
	bool success = true;

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
			if (FAILED(hr)) {
				Logger::Output(LogType::Engine, "CreateGraphicsPipelineState failed: {} [{}]", desc.pixel.file, mode);
				success = false;
				continue;
			}

			const std::string psoName =
				Algorithm::PathToUTF8(Algorithm::PathFromUTF8(desc.preRaster.file).stem()) +
				"|" + Algorithm::PathToUTF8(Algorithm::PathFromUTF8(desc.pixel.file).stem()) +
				"[" + mode + "]";
			graphicsPipelines_[static_cast<uint32_t>(blendMode)]->SetName(Algorithm::ConvertString(psoName).c_str());
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
			if (FAILED(hr)) {
				Logger::Output(LogType::Engine, "CreatePipelineState failed: {} [{}]", desc.pixel.file, mode);
				success = false;
				continue;
			}

			const std::string psoName =
				Algorithm::PathToUTF8(Algorithm::PathFromUTF8(desc.preRaster.file).stem()) +
				"|" + Algorithm::PathToUTF8(Algorithm::PathFromUTF8(desc.pixel.file).stem()) +
				"[" + mode + "]";
			graphicsPipelines_[static_cast<uint32_t>(blendMode)]->SetName(Algorithm::ConvertString(psoName).c_str());
		}
		break;
	}
	}
	Logger::Output(LogType::Engine, success ? "Created GraphicsPipeline" : "Failed GraphicsPipeline creation");
	Logger::EndSection(LogType::Engine);
	return success;
}

bool Engine::PipelineState::CreateCompute(ID3D12Device8* device, DxShaderCompiler* compiler, const ComputePipelineDesc& desc) {

	Logger::BeginSection(LogType::Engine);
	Logger::Output(LogType::Engine, "Start CreateComputePipeline: {}", desc.compute.file);

	// シェーダーのコンパイル
	const std::filesystem::path shaderPath = ResolveShaderPath(desc.compute.file);
	if (shaderPath.empty()) {
		Logger::Output(LogType::Engine, "[PostProcess] Compute shader file not found: {}", desc.compute.file);
		Logger::EndSection(LogType::Engine);
		return false;
	}
	const std::wstring entry = ResolveEntry(desc.compute.entry);
	const std::wstring profile = ResolveProfile(desc.compute.profile, ShaderStage::CS);
	CompiledShader shader = compiler->CompileShader(shaderPath.wstring(), profile.c_str(), entry.c_str(), ShaderStage::CS);
	if (!shader.object) {
		Logger::Output(LogType::Engine, "[PostProcess] Shader compilation failed: {}", desc.compute.file);
		Logger::EndSection(LogType::Engine);
		return false;
	}
	Logger::Output(LogType::Engine, "Finished compiling CS for {}",
		Algorithm::PathToUTF8(shaderPath));

	// スレッドサイズを設定
	threadGroupX_ = shader.reflection.threadGroupX;
	threadGroupY_ = shader.reflection.threadGroupY;
	threadGroupZ_ = shader.reflection.threadGroupZ;
	computeReflection_ = shader.reflection;

	Logger::Output(LogType::Engine, "ThreadGroup[{},{},{}]", threadGroupX_, threadGroupY_, threadGroupZ_);

	// ルートシグネイチャの自動生成
	AutoRootSignatureBuilder builder;
	const std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers =
		BuildComputeStaticSamplers(shader.reflection, desc.staticSamplers, desc.staticSamplerOverrides);
	auto rootSignatureResult = builder.Build(device, PipelineType::Compute, { &shader }, staticSamplers);
	// 結果を設定
	rootSignature_ = rootSignatureResult.rootSignature;
	bindings_ = std::move(rootSignatureResult.bindings);
	RebuildBindingLookupTables();

	D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = rootSignature_.Get();
	pipelineDesc.CS = ToBytecode(&shader);

	// パイプラインステートオブジェクトの生成
	HRESULT hr = device->CreateComputePipelineState(&pipelineDesc, IID_PPV_ARGS(&computePipeline_));
	if (FAILED(hr)) {
		Logger::Output(LogType::Engine, "[PostProcess] CreateComputePipelineState failed: {}", desc.compute.file);
		Logger::EndSection(LogType::Engine);
		return false;
	}
	computePipeline_->SetName(Algorithm::ConvertString(Algorithm::PathToUTF8(
		Algorithm::PathFromUTF8(desc.compute.file).stem())).c_str());
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
