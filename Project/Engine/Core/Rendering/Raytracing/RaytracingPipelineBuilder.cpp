#include "RaytracingPipelineBuilder.h"

//============================================================================
//	include
//============================================================================
#include "RaytracingShaderLibrary.h"
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
#include <Engine/Core/Rendering/Shaders/ShaderCook.h>

// c++
#include <algorithm>
#include <atomic>
#include <cstring>

namespace {

	// 指定境界へ切り上げる

	// コンパイル結果からD3D12シェーダーバイトコードを構築する
	D3D12_SHADER_BYTECODE ToByteCode(
		const Engine::CompiledShader& shader) {

		D3D12_SHADER_BYTECODE byteCode{};
		byteCode.pShaderBytecode = shader.GetBytecodePointer();
		byteCode.BytecodeLength = shader.GetBytecodeSize();
		return byteCode;
	}

	// DXRライブラリをCookまたはソースから読み込む

	// エクスポート名を重複なく追加する
	void AddUniqueExport(std::vector<std::string>& exports,
		const std::string& name) {

		if (!name.empty() && std::find(exports.begin(), exports.end(), name) ==
			exports.end()) {

			exports.emplace_back(name);
		}
	}

	// ヒットグループ種別をDXR形式へ変換する
	D3D12_HIT_GROUP_TYPE ToDxHitGroupType(
		Engine::RaytracingHitGroupKind kind) {

		return kind == Engine::RaytracingHitGroupKind::Procedural ?
			D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE :
			D3D12_HIT_GROUP_TYPE_TRIANGLES;
	}

	// State Object生成失敗時にD3D12側の具体的な理由をログへ残す
	void LogStateObjectMessages(ID3D12InfoQueue* infoQueue) {

		if (!infoQueue) {
			return;
		}
		const UINT64 messageCount = infoQueue->GetNumStoredMessages();
		for (UINT64 index = 0; index < messageCount; ++index) {

			SIZE_T messageSize = 0;
			infoQueue->GetMessage(index, nullptr, &messageSize);
			if (messageSize == 0) {
				continue;
			}
			std::vector<uint8_t> storage(messageSize);
			auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
			if (SUCCEEDED(infoQueue->GetMessage(index, message, &messageSize)) &&
				message->pDescription) {

				Engine::Logger::Output(Engine::LogType::Engine,
					spdlog::level::err, "[レイトレーシングパイプライン] D3D12: {}",
					message->pDescription);
			}
		}
	}
}

std::unique_ptr<Engine::RaytracingPipelineState> Engine::RaytracingPipelineBuilder::Create(
	ID3D12Device8* device, DxShaderCompiler* compiler, const PipelineVariantDesc& variant,
	const ShaderAsset& shaderAsset, const PipelineStaticSamplerOverrideSet* samplerOverrides) {

	auto state = std::make_unique<RaytracingPipelineState>();
	if (!Build(*state, device, compiler, variant, shaderAsset, samplerOverrides)) {
		return nullptr;
	}
	return state;
}

bool Engine::RaytracingPipelineBuilder::Build(RaytracingPipelineState& state,
	ID3D12Device8* device, DxShaderCompiler* compiler,
	const PipelineVariantDesc& variant,
	const ShaderAsset& shaderAsset,
	const PipelineStaticSamplerOverrideSet* samplerOverrides) {

	state.stateObject_.Reset();
	state.stateProps_.Reset();
	state.globalRootSignature_.Reset();
	state.shaderTable_.Reset();
	state.bindings_.clear();
	state.reflection_ = {};
	state.shaderTableSize_ = 0;
	state.rayGenerationCount_ = 0;
	state.missCount_ = 0;
	state.hitGroupCount_ = 0;
	state.callableCount_ = 0;

	if (!BuildStateObject(state, device, compiler, variant, shaderAsset,
		samplerOverrides)) {
		return false;
	}
	Logger::Output(LogType::Engine,
		"[レイトレーシングパイプライン] State Objectを作成しました RayGeneration={} Miss={} HitGroup={} Callable={}",
		state.rayGenerationCount_, state.missCount_, state.hitGroupCount_, state.callableCount_);
	return true;
}

bool Engine::RaytracingPipelineBuilder::BuildGlobalRootSignature(RaytracingPipelineState& state,
	ID3D12Device8* device,
	const std::vector<const CompiledShader*>& shaders,
	const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers) {

	AutoRootSignatureBuilder builder{};
	RootSignatureBuildResult result = builder.Build(device,
		PipelineType::Raytracing, shaders, staticSamplers);
	if (!result.rootSignature) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レイトレーシングパイプライン] グローバルルートシグネチャの作成に失敗しました");
		return false;
	}
	state.globalRootSignature_ = std::move(result.rootSignature);
	state.bindings_ = std::move(result.bindings);
	return true;
}

bool Engine::RaytracingPipelineBuilder::BuildStateObject(RaytracingPipelineState& state,
	ID3D12Device8* device, DxShaderCompiler* compiler,
	const PipelineVariantDesc& variant,
	const ShaderAsset& shaderAsset,
	const PipelineStaticSamplerOverrideSet* samplerOverrides) {

	if (variant.rayGenerationExports.empty() || variant.missExports.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レイトレーシングパイプライン] RayGenerationとMissのExportが必要です");
		return false;
	}

	std::vector<std::string> libraryExports{};
	for (const std::string& name : variant.rayGenerationExports) {
		AddUniqueExport(libraryExports, name);
	}
	for (const std::string& name : variant.missExports) {
		AddUniqueExport(libraryExports, name);
	}
	for (const std::string& name : variant.callableExports) {
		AddUniqueExport(libraryExports, name);
	}
	for (const RaytracingHitGroupDesc& group : variant.hitGroups) {
		AddUniqueExport(libraryExports, group.closestHitExport);
		AddUniqueExport(libraryExports, group.anyHitExport);
		AddUniqueExport(libraryExports, group.intersectionExport);
		if (group.kind == RaytracingHitGroupKind::Procedural &&
			group.intersectionExport.empty()) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レイトレーシングパイプライン] Procedural Hit GroupにはIntersection Exportが必要です: {}",
				group.exportName);
			return false;
		}
	}

	std::vector<CompiledShader> compiledShaders{};
	compiledShaders.reserve(libraryExports.size());
	for (const std::string& exportName : libraryExports) {
		const ShaderStageEntry* stage =
			FindShaderExport(shaderAsset, ShaderStage::Lib, exportName);
		if (!stage) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レイトレーシングパイプライン] Exportが見つかりません: {}", exportName);
			return false;
		}
		CompiledShader compiled = RaytracingShaderLibrary::Load(compiler, *stage);
		if (!compiled.IsValid()) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レイトレーシングパイプライン] ライブラリのコンパイルに失敗しました: {}", exportName);
			return false;
		}
		compiledShaders.emplace_back(std::move(compiled));
	}
	std::vector<const CompiledShader*> compiledPointers{};
	compiledPointers.reserve(compiledShaders.size());
	for (const CompiledShader& compiled : compiledShaders) {
		compiledPointers.emplace_back(&compiled);
		MergeShaderReflection(state.reflection_, compiled.reflection);
	}
	ApplyShaderParameterMetadata(state.reflection_, shaderAsset);
	const std::vector<D3D12_STATIC_SAMPLER_DESC> staticSamplers =
		samplerOverrides ? BuildPipelineStaticSamplers(state.reflection_,
			variant.staticSamplers, *samplerOverrides) : variant.staticSamplers;
	if (!BuildGlobalRootSignature(state, device, compiledPointers, staticSamplers)) {
		return false;
	}

	std::vector<std::wstring> libraryExportNames{};
	libraryExportNames.reserve(libraryExports.size());
	for (const std::string& name : libraryExports) {
		libraryExportNames.emplace_back(Algorithm::ConvertString(name));
	}
	std::vector<D3D12_EXPORT_DESC> exportDescs{};
	std::vector<D3D12_DXIL_LIBRARY_DESC> libraryDescs{};
	std::vector<D3D12_HIT_GROUP_DESC> hitGroupDescs{};
	std::vector<D3D12_STATE_SUBOBJECT> subobjects{};
	exportDescs.reserve(compiledShaders.size());
	libraryDescs.reserve(compiledShaders.size());
	hitGroupDescs.reserve(variant.hitGroups.size());
	subobjects.reserve(compiledShaders.size() + variant.hitGroups.size() + 4);

	for (size_t index = 0; index < compiledShaders.size(); ++index) {
		exportDescs.push_back({
			libraryExportNames[index].c_str(), nullptr, D3D12_EXPORT_FLAG_NONE });
		D3D12_DXIL_LIBRARY_DESC libraryDesc{};
		libraryDesc.DXILLibrary = ToByteCode(compiledShaders[index]);
		libraryDesc.NumExports = 1;
		libraryDesc.pExports = &exportDescs.back();
		libraryDescs.emplace_back(libraryDesc);
		D3D12_STATE_SUBOBJECT subobject{};
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		subobject.pDesc = &libraryDescs.back();
		subobjects.emplace_back(subobject);
	}

	std::vector<std::wstring> hitGroupExports{};
	std::vector<std::wstring> closestHitExports{};
	std::vector<std::wstring> anyHitExports{};
	std::vector<std::wstring> intersectionExports{};
	hitGroupExports.reserve(variant.hitGroups.size());
	closestHitExports.reserve(variant.hitGroups.size());
	anyHitExports.reserve(variant.hitGroups.size());
	intersectionExports.reserve(variant.hitGroups.size());
	for (const RaytracingHitGroupDesc& group : variant.hitGroups) {
		hitGroupExports.emplace_back(Algorithm::ConvertString(group.exportName));
		closestHitExports.emplace_back(
			Algorithm::ConvertString(group.closestHitExport));
		anyHitExports.emplace_back(Algorithm::ConvertString(group.anyHitExport));
		intersectionExports.emplace_back(
			Algorithm::ConvertString(group.intersectionExport));
		D3D12_HIT_GROUP_DESC desc{};
		desc.HitGroupExport = hitGroupExports.back().c_str();
		desc.ClosestHitShaderImport = closestHitExports.back().empty() ?
			nullptr : closestHitExports.back().c_str();
		desc.AnyHitShaderImport = anyHitExports.back().empty() ?
			nullptr : anyHitExports.back().c_str();
		desc.IntersectionShaderImport = intersectionExports.back().empty() ?
			nullptr : intersectionExports.back().c_str();
		desc.Type = ToDxHitGroupType(group.kind);
		hitGroupDescs.emplace_back(desc);
		D3D12_STATE_SUBOBJECT subobject{};
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
		subobject.pDesc = &hitGroupDescs.back();
		subobjects.emplace_back(subobject);
	}

	D3D12_GLOBAL_ROOT_SIGNATURE globalRootDesc{
		state.globalRootSignature_.Get() };
	D3D12_STATE_SUBOBJECT globalRootSubobject{};
	globalRootSubobject.Type =
		D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
	globalRootSubobject.pDesc = &globalRootDesc;
	subobjects.emplace_back(globalRootSubobject);

	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
	shaderConfig.MaxPayloadSizeInBytes = variant.maxPayloadSizeInBytes;
	shaderConfig.MaxAttributeSizeInBytes = variant.maxAttributeSizeInBytes;
	D3D12_STATE_SUBOBJECT shaderConfigSubobject{};
	shaderConfigSubobject.Type =
		D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
	shaderConfigSubobject.pDesc = &shaderConfig;
	subobjects.emplace_back(shaderConfigSubobject);
	const D3D12_STATE_SUBOBJECT* shaderConfigSubobjectPointer =
		&subobjects.back();

	std::vector<std::wstring> rayGenerationExports{};
	std::vector<std::wstring> missExports{};
	std::vector<std::wstring> callableExports{};
	for (const std::string& name : variant.rayGenerationExports) {
		rayGenerationExports.emplace_back(Algorithm::ConvertString(name));
	}
	for (const std::string& name : variant.missExports) {
		missExports.emplace_back(Algorithm::ConvertString(name));
	}
	for (const std::string& name : variant.callableExports) {
		callableExports.emplace_back(Algorithm::ConvertString(name));
	}
	std::vector<LPCWSTR> shaderConfigExports{};
	shaderConfigExports.reserve(rayGenerationExports.size() + missExports.size() +
		callableExports.size() + hitGroupExports.size());
	for (const std::wstring& name : rayGenerationExports) {
		shaderConfigExports.emplace_back(name.c_str());
	}
	for (const std::wstring& name : missExports) {
		shaderConfigExports.emplace_back(name.c_str());
	}
	for (const std::wstring& name : callableExports) {
		shaderConfigExports.emplace_back(name.c_str());
	}
	for (const std::wstring& name : hitGroupExports) {
		shaderConfigExports.emplace_back(name.c_str());
	}
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigAssociation{};
	shaderConfigAssociation.pSubobjectToAssociate =
		shaderConfigSubobjectPointer;
	shaderConfigAssociation.NumExports =
		static_cast<UINT>(shaderConfigExports.size());
	shaderConfigAssociation.pExports = shaderConfigExports.data();
	D3D12_STATE_SUBOBJECT associationSubobject{};
	associationSubobject.Type =
		D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
	associationSubobject.pDesc = &shaderConfigAssociation;
	subobjects.emplace_back(associationSubobject);

	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
	pipelineConfig.MaxTraceRecursionDepth = variant.maxRecursionDepth;
	D3D12_STATE_SUBOBJECT pipelineConfigSubobject{};
	pipelineConfigSubobject.Type =
		D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
	pipelineConfigSubobject.pDesc = &pipelineConfig;
	subobjects.emplace_back(pipelineConfigSubobject);

	D3D12_STATE_OBJECT_DESC stateObjectDesc{};
	stateObjectDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	stateObjectDesc.NumSubobjects = static_cast<UINT>(subobjects.size());
	stateObjectDesc.pSubobjects = subobjects.data();
	ComPtr<ID3D12InfoQueue> infoQueue{};
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		infoQueue->ClearStoredMessages();
	}
	HRESULT result = device->CreateStateObject(
		&stateObjectDesc, IID_PPV_ARGS(&state.stateObject_));
	if (FAILED(result) || !state.stateObject_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レイトレーシングパイプライン] State Objectの作成に失敗しました HRESULT=0x{:08X}",
			static_cast<uint32_t>(result));
		LogStateObjectMessages(infoQueue.Get());
		return false;
	}
	result = state.stateObject_->QueryInterface(IID_PPV_ARGS(&state.stateProps_));
	if (FAILED(result) || !state.stateProps_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レイトレーシングパイプライン] State Object Propertiesの取得に失敗しました HRESULT=0x{:08X}",
			static_cast<uint32_t>(result));
		state.stateObject_.Reset();
		return false;
	}
	return BuildShaderTable(state, device, rayGenerationExports,
		missExports, hitGroupExports, callableExports);
}
