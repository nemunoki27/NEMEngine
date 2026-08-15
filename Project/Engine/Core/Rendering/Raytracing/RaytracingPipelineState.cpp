#include "RaytracingPipelineState.h"

//============================================================================
//	include
//============================================================================
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
	UINT64 AlignUp(UINT64 value, UINT64 alignment) {

		return (value + alignment - 1) & ~(alignment - 1);
	}

	// コンパイル結果からD3D12シェーダーバイトコードを構築する
	D3D12_SHADER_BYTECODE ToByteCode(
		const Engine::CompiledShader& shader) {

		D3D12_SHADER_BYTECODE byteCode{};
		byteCode.pShaderBytecode = shader.GetBytecodePointer();
		byteCode.BytecodeLength = shader.GetBytecodeSize();
		return byteCode;
	}

	// DXRライブラリをCookまたはソースから読み込む
	Engine::CompiledShader LoadRaytracingLibrary(
		Engine::DxShaderCompiler* compiler,
		const Engine::ShaderStageEntry& stage) {

		const std::string entry = stage.entry.empty() ? "main" : stage.entry;
		const std::string profile = stage.profile.empty() ? "lib_6_6" : stage.profile;
		Engine::CompiledShader shader{};
		if (stage.ownerShader && Engine::ShaderCook::Load({
			.shader = stage.ownerShader,
			.stage = Engine::ShaderStage::Lib,
			.entry = entry,
			.profile = profile,
			}, shader)) {

			Engine::Logger::Output(Engine::LogType::Engine,
				"[ShaderCook] Loaded DXR shader={} entry={}",
				Engine::ToString(stage.ownerShader), entry);
			return shader;
		}
		if (Engine::ShaderCook::IsCookedProduct()) {
			Engine::Logger::Output(Engine::LogType::Engine,
				spdlog::level::err,
				"[ShaderCook] Missing cooked DXR shader={} entry={} profile={}",
				Engine::ToString(stage.ownerShader), entry, profile);
			return {};
		}

		const std::filesystem::path path =
			Engine::ShaderSourcePath::Resolve(stage.file);
		if (!compiler || path.empty()) {
			return {};
		}
		return compiler->CompileShader(path.wstring(),
			Engine::Algorithm::ConvertString(profile).c_str(),
			Engine::Algorithm::ConvertString(entry).c_str(),
			Engine::ShaderStage::Lib);
	}

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
					spdlog::level::err, "[RaytracingPipeline] D3D12: {}",
					message->pDescription);
			}
		}
	}
}

//============================================================================
//	RaytracingPipelineState classMethods
//============================================================================
uint64_t Engine::RaytracingPipelineState::NextUniqueID() {

	static std::atomic<uint64_t> counter{ 0 };
	return ++counter;
}

bool Engine::RaytracingPipelineState::Create(
	ID3D12Device8* device, DxShaderCompiler* compiler,
	const PipelineVariantDesc& variant,
	const ShaderAsset& shaderAsset) {

	stateObject_.Reset();
	stateProps_.Reset();
	globalRootSignature_.Reset();
	shaderTable_.Reset();
	bindings_.clear();
	reflection_ = {};
	shaderTableSize_ = 0;
	rayGenerationCount_ = 0;
	missCount_ = 0;
	hitGroupCount_ = 0;
	callableCount_ = 0;

	if (!BuildStateObject(device, compiler, variant, shaderAsset)) {
		return false;
	}
	Logger::Output(LogType::Engine,
		"[RaytracingPipeline] Created state object rayGen={} miss={} hitGroup={} callable={}",
		rayGenerationCount_, missCount_, hitGroupCount_, callableCount_);
	return true;
}

D3D12_DISPATCH_RAYS_DESC Engine::RaytracingPipelineState::BuildDispatchDesc(
	uint32_t width, uint32_t height, uint32_t depth,
	uint32_t rayGenerationIndex) const {

	D3D12_DISPATCH_RAYS_DESC desc{};
	if (!shaderTable_ || rayGenerationIndex >= rayGenerationCount_) {
		return desc;
	}
	const D3D12_GPU_VIRTUAL_ADDRESS baseAddress =
		shaderTable_->GetGPUVirtualAddress();
	desc.RayGenerationShaderRecord.StartAddress = baseAddress +
		rayGenerationTableOffset_ + rayGenerationIndex * kRecordStride;
	desc.RayGenerationShaderRecord.SizeInBytes = kRecordStride;
	if (missCount_ > 0) {
		desc.MissShaderTable.StartAddress = baseAddress + missTableOffset_;
		desc.MissShaderTable.SizeInBytes = missCount_ * kRecordStride;
		desc.MissShaderTable.StrideInBytes = kRecordStride;
	}
	if (hitGroupCount_ > 0) {
		desc.HitGroupTable.StartAddress = baseAddress + hitGroupTableOffset_;
		desc.HitGroupTable.SizeInBytes = hitGroupCount_ * kRecordStride;
		desc.HitGroupTable.StrideInBytes = kRecordStride;
	}
	if (callableCount_ > 0) {
		desc.CallableShaderTable.StartAddress = baseAddress + callableTableOffset_;
		desc.CallableShaderTable.SizeInBytes = callableCount_ * kRecordStride;
		desc.CallableShaderTable.StrideInBytes = kRecordStride;
	}
	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	return desc;
}

const Engine::RootBindingLocation*
Engine::RaytracingPipelineState::FindBindingByName(
	std::string_view name, ShaderBindingKind kind) const {

	const auto found = std::find_if(bindings_.begin(), bindings_.end(),
		[&](const RootBindingLocation& binding) {
			return binding.kind == kind && binding.name == name;
		});
	return found != bindings_.end() ? &*found : nullptr;
}

bool Engine::RaytracingPipelineState::BuildGlobalRootSignature(
	ID3D12Device8* device,
	const std::vector<const CompiledShader*>& shaders,
	const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers) {

	AutoRootSignatureBuilder builder{};
	RootSignatureBuildResult result = builder.Build(device,
		PipelineType::Raytracing, shaders, staticSamplers);
	if (!result.rootSignature) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[RaytracingPipeline] Global root signature creation failed");
		return false;
	}
	globalRootSignature_ = std::move(result.rootSignature);
	bindings_ = std::move(result.bindings);
	return true;
}

bool Engine::RaytracingPipelineState::BuildStateObject(
	ID3D12Device8* device, DxShaderCompiler* compiler,
	const PipelineVariantDesc& variant,
	const ShaderAsset& shaderAsset) {

	if (variant.rayGenerationExports.empty() || variant.missExports.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[RaytracingPipeline] RayGeneration and Miss exports are required");
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
				"[RaytracingPipeline] Procedural hit group requires Intersection export: {}",
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
				"[RaytracingPipeline] Export was not found: {}", exportName);
			return false;
		}
		CompiledShader compiled = LoadRaytracingLibrary(compiler, *stage);
		if (!compiled.IsValid()) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[RaytracingPipeline] Library compilation failed: {}", exportName);
			return false;
		}
		compiledShaders.emplace_back(std::move(compiled));
	}
	std::vector<const CompiledShader*> compiledPointers{};
	compiledPointers.reserve(compiledShaders.size());
	for (const CompiledShader& compiled : compiledShaders) {
		compiledPointers.emplace_back(&compiled);
		MergeShaderReflection(reflection_, compiled.reflection);
	}
	ApplyShaderParameterMetadata(reflection_, shaderAsset);
	if (!BuildGlobalRootSignature(device, compiledPointers,
		variant.staticSamplers)) {
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
		globalRootSignature_.Get() };
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
		&stateObjectDesc, IID_PPV_ARGS(&stateObject_));
	if (FAILED(result) || !stateObject_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[RaytracingPipeline] Failed to create state object. HRESULT=0x{:08X}",
			static_cast<uint32_t>(result));
		LogStateObjectMessages(infoQueue.Get());
		return false;
	}
	result = stateObject_->QueryInterface(IID_PPV_ARGS(&stateProps_));
	if (FAILED(result) || !stateProps_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[RaytracingPipeline] Failed to query state object properties. HRESULT=0x{:08X}",
			static_cast<uint32_t>(result));
		stateObject_.Reset();
		return false;
	}
	return BuildShaderTable(device, rayGenerationExports,
		missExports, hitGroupExports, callableExports);
}

bool Engine::RaytracingPipelineState::BuildShaderTable(
	ID3D12Device8* device,
	const std::vector<std::wstring>& rayGenerationExports,
	const std::vector<std::wstring>& missExports,
	const std::vector<std::wstring>& hitGroupExports,
	const std::vector<std::wstring>& callableExports) {

	rayGenerationCount_ = static_cast<uint32_t>(rayGenerationExports.size());
	missCount_ = static_cast<uint32_t>(missExports.size());
	hitGroupCount_ = static_cast<uint32_t>(hitGroupExports.size());
	callableCount_ = static_cast<uint32_t>(callableExports.size());
	rayGenerationTableOffset_ = 0;
	missTableOffset_ = AlignUp(rayGenerationCount_ * kRecordStride, kTableAlign);
	hitGroupTableOffset_ = AlignUp(
		missTableOffset_ + missCount_ * kRecordStride, kTableAlign);
	callableTableOffset_ = AlignUp(
		hitGroupTableOffset_ + hitGroupCount_ * kRecordStride, kTableAlign);
	shaderTableSize_ = static_cast<UINT>(AlignUp(
		callableTableOffset_ + callableCount_ * kRecordStride, kTableAlign));

	CD3DX12_RESOURCE_DESC bufferDesc =
		CD3DX12_RESOURCE_DESC::Buffer(shaderTableSize_);
	CD3DX12_HEAP_PROPERTIES heapProperties(D3D12_HEAP_TYPE_UPLOAD);
	HRESULT result = device->CreateCommittedResource(&heapProperties,
		D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr, IID_PPV_ARGS(&shaderTable_));
	if (FAILED(result) || !shaderTable_) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[RaytracingPipeline] Failed to create shader table. HRESULT=0x{:08X}",
			static_cast<uint32_t>(result));
		return false;
	}

	uint8_t* mapped = nullptr;
	result = shaderTable_->Map(0, nullptr,
		reinterpret_cast<void**>(&mapped));
	if (FAILED(result) || !mapped) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[RaytracingPipeline] Failed to map shader table. HRESULT=0x{:08X}",
			static_cast<uint32_t>(result));
		shaderTable_.Reset();
		return false;
	}
	std::memset(mapped, 0, shaderTableSize_);
	const auto writeTable = [&](const std::vector<std::wstring>& exports,
		UINT64 tableOffset) {

		for (size_t index = 0; index < exports.size(); ++index) {
			const void* identifier =
				stateProps_->GetShaderIdentifier(exports[index].c_str());
			if (!identifier) {
				return false;
			}
			std::memcpy(mapped + tableOffset + index * kRecordStride,
				identifier, kHandleSize);
		}
		return true;
	};
	const bool written =
		writeTable(rayGenerationExports, rayGenerationTableOffset_) &&
		writeTable(missExports, missTableOffset_) &&
		writeTable(hitGroupExports, hitGroupTableOffset_) &&
		writeTable(callableExports, callableTableOffset_);
	shaderTable_->Unmap(0, nullptr);
	if (!written) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[RaytracingPipeline] Shader identifier was not found");
		shaderTable_.Reset();
		return false;
	}
	return true;
}
