#include "RaytracingPipelineState.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>
#include <Engine/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Runtime/RuntimePaths.h>

//============================================================================
//	RaytracingPipelineState classMethods
//============================================================================

namespace {
	// シェーダーファイルのパスを解決する関数
	std::filesystem::path ResolveShaderPath(const std::string& file) {

		const std::filesystem::path resolved = Engine::RuntimePaths::ResolveAssetPath(file);
		if (std::filesystem::exists(resolved) && std::filesystem::is_regular_file(resolved)) {
			return resolved;
		}

		const std::filesystem::path base = Engine::RuntimePaths::GetEngineAssetPath("Shaders");
		std::filesystem::path direct = base / file;
		if (std::filesystem::exists(direct)) {
			return direct;
		}
		std::filesystem::path raw(file);
		if (std::filesystem::exists(raw)) {
			return raw;
		}
		return {};
	}
	// シェーダーアセットから指定されたエントリーポイントを持つライブラリステージを探す関数
	const Engine::ShaderStageEntry* FindLibraryStageByEntry(const Engine::ShaderAsset& shaderAsset, const std::string_view& entryName) {

		for (const auto& stage : shaderAsset.stages) {
			if (Engine::Algorithm::HasFlag<Engine::ShaderStage>(stage.stage, Engine::ShaderStage::Lib) &&
				stage.entry == entryName) {
				return &stage;
			}
		}
		return nullptr;
	}
	// シェーダーのコンパイル結果からD3D12_SHADER_BYTECODEを構築する関数
	D3D12_SHADER_BYTECODE ToByteCode(const Engine::CompiledShader& shader) {

		D3D12_SHADER_BYTECODE byteCode{};
		byteCode.pShaderBytecode = shader.object->GetBufferPointer();
		byteCode.BytecodeLength = shader.object->GetBufferSize();
		return byteCode;
	}
}

void Engine::RaytracingPipelineState::Create(ID3D12Device8* device, DxShaderCompiler* compiler,
	const PipelineVariantDesc& variant, const ShaderAsset& shaderAsset) {

	BuildGlobalRootSignature(device);
	BuildStateObject(device, compiler, variant, shaderAsset);
}

D3D12_DISPATCH_RAYS_DESC Engine::RaytracingPipelineState::BuildDispatchDesc(uint32_t width,
	uint32_t height, uint32_t depth) const {

	D3D12_DISPATCH_RAYS_DESC desc{};
	const D3D12_GPU_VIRTUAL_ADDRESS baseAddress = shaderTable_->GetGPUVirtualAddress();

	desc.RayGenerationShaderRecord.StartAddress = baseAddress + kRayGenOffset;
	desc.RayGenerationShaderRecord.SizeInBytes = kRecordStride;

	desc.MissShaderTable.StartAddress = baseAddress + kMissOffset;
	desc.MissShaderTable.SizeInBytes = kRecordStride;
	desc.MissShaderTable.StrideInBytes = kRecordStride;

	desc.HitGroupTable.StartAddress = baseAddress + kHitGroupOffset;
	desc.HitGroupTable.SizeInBytes = kRecordStride;
	desc.HitGroupTable.StrideInBytes = kRecordStride;

	desc.Width = width;
	desc.Height = height;
	desc.Depth = depth;
	return desc;
}

void Engine::RaytracingPipelineState::BuildGlobalRootSignature(ID3D12Device8* device) {

	CD3DX12_DESCRIPTOR_RANGE sourceColorRange{};
	sourceColorRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_DESCRIPTOR_RANGE sourceDepthRange{};
	sourceDepthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

	CD3DX12_DESCRIPTOR_RANGE sourceNormalRange{};
	sourceNormalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

	CD3DX12_DESCRIPTOR_RANGE sourcePositionRange{};
	sourcePositionRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

	CD3DX12_DESCRIPTOR_RANGE sceneInstancesRange{};
	sceneInstancesRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);

	CD3DX12_DESCRIPTOR_RANGE sceneSubMeshesRange{};
	sceneSubMeshesRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);

	CD3DX12_DESCRIPTOR_RANGE destRange{};
	destRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	// ルートパラメータの構築
	CD3DX12_ROOT_PARAMETER rootParameters[9]{};
	rootParameters[kRootIndexTLAS].InitAsShaderResourceView(0);                              // t0
	rootParameters[kRootIndexSourceColor].InitAsDescriptorTable(1, &sourceColorRange);       // t1
	rootParameters[kRootIndexSourceDepth].InitAsDescriptorTable(1, &sourceDepthRange);       // t2
	rootParameters[kRootIndexSourceNormal].InitAsDescriptorTable(1, &sourceNormalRange);     // t3
	rootParameters[kRootIndexSourcePosition].InitAsDescriptorTable(1, &sourcePositionRange); // t4
	rootParameters[kRootIndexSceneInstances].InitAsDescriptorTable(1, &sceneInstancesRange); // t5
	rootParameters[kRootIndexSceneSubMeshes].InitAsDescriptorTable(1, &sceneSubMeshesRange); // t6
	rootParameters[kRootIndexDestUAV].InitAsDescriptorTable(1, &destRange);                  // u0
	rootParameters[kRootIndexViewCBV].InitAsConstantBufferView(0);                           // b0
	// 静的サンプラーの構築
	D3D12_STATIC_SAMPLER_DESC staticSampler{};
	staticSampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	staticSampler.ShaderRegister = 0;
	staticSampler.RegisterSpace = 0;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
	staticSampler.MaxLOD = D3D12_FLOAT32_MAX;

	D3D12_ROOT_SIGNATURE_DESC desc{};
	desc.NumParameters = _countof(rootParameters);
	desc.pParameters = rootParameters;
	desc.NumStaticSamplers = 1;
	desc.pStaticSamplers = &staticSampler;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;

	// ルートシグネチャのシリアライズと生成
	ComPtr<ID3DBlob> sigBlob;
	ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob) {
			OutputDebugStringA(static_cast<const char*>(errorBlob->GetBufferPointer()));
		}
		Assert::Call(false, "Failed to serialize raytracing global root signature");
	}
	hr = device->CreateRootSignature(0, sigBlob->GetBufferPointer(), sigBlob->GetBufferSize(), IID_PPV_ARGS(&globalRootSignature_));
	Assert::Call(SUCCEEDED(hr), "Failed to create raytracing global root signature");
}

void Engine::RaytracingPipelineState::BuildStateObject(ID3D12Device8* device,
	DxShaderCompiler* compiler, const PipelineVariantDesc& variant, const ShaderAsset& shaderAsset) {

	// シェーダーアセットから必要なステージを探す
	const ShaderStageEntry* rayGenStage = FindLibraryStageByEntry(shaderAsset, variant.rayGenerationExport);
	const ShaderStageEntry* missStage = FindLibraryStageByEntry(shaderAsset, variant.missExport);
	const ShaderStageEntry* closestHitStage = FindLibraryStageByEntry(shaderAsset, variant.closestHitExport);
	// エントリーポイントが見つからない場合はエラー
	Assert::Call(rayGenStage != nullptr, "RayGeneration export was not found in ShaderAsset");
	Assert::Call(missStage != nullptr, "Miss export was not found in ShaderAsset");
	Assert::Call(closestHitStage != nullptr, "ClosestHit export was not found in ShaderAsset");

	// シェーダーファイルのパスを取得
	const std::filesystem::path rayGenPath = ResolveShaderPath(rayGenStage->file);
	const std::filesystem::path missPath = ResolveShaderPath(missStage->file);
	const std::filesystem::path hitPath = ResolveShaderPath(closestHitStage->file);
	// シェーダーファイルが見つからない場合はエラー
	Assert::Call(!rayGenPath.empty(), "RayGeneration shader file not found");
	Assert::Call(!missPath.empty(), "Miss shader file not found");
	Assert::Call(!hitPath.empty(), "ClosestHit shader file not found");

	// シェーダーのコンパイル
	CompiledShader rayGenShader = compiler->CompileShader(rayGenPath.wstring(),
		Algorithm::ConvertString(rayGenStage->profile.empty() ? std::string("lib_6_6") : rayGenStage->profile).c_str(),
		Algorithm::ConvertString(rayGenStage->entry).c_str(), ShaderStage::Lib);
	CompiledShader missShader = compiler->CompileShader(missPath.wstring(),
		Algorithm::ConvertString(missStage->profile.empty() ? std::string("lib_6_6") : missStage->profile).c_str(),
		Algorithm::ConvertString(missStage->entry).c_str(), ShaderStage::Lib);
	CompiledShader closestHitShader = compiler->CompileShader(hitPath.wstring(),
		Algorithm::ConvertString(closestHitStage->profile.empty() ? std::string("lib_6_6") : closestHitStage->profile).c_str(),
		Algorithm::ConvertString(closestHitStage->entry).c_str(), ShaderStage::Lib);
	// wstringに変換
	std::wstring rayGenExport = Algorithm::ConvertString(variant.rayGenerationExport);
	std::wstring missExport = Algorithm::ConvertString(variant.missExport);
	std::wstring closestHitExport = Algorithm::ConvertString(variant.closestHitExport);
	std::wstring hitGroupExport = Algorithm::ConvertString(variant.hitGroupExport);
	std::wstring anyHitExport{};
	if (!variant.anyHitExport.empty()) {

		anyHitExport = Algorithm::ConvertString(variant.anyHitExport);
	}

	// サブオブジェクトの構築
	std::vector<D3D12_EXPORT_DESC> exportDescs;
	exportDescs.reserve(3);
	std::vector<D3D12_DXIL_LIBRARY_DESC> libraryDescs;
	libraryDescs.reserve(3);
	std::vector<D3D12_STATE_SUBOBJECT> subobjects;
	subobjects.reserve(12);
	auto addLibrary = [&](const CompiledShader& shader, const std::wstring& exportName) {

		exportDescs.push_back({ exportName.c_str(), nullptr, D3D12_EXPORT_FLAG_NONE });
		D3D12_DXIL_LIBRARY_DESC libDesc{};
		libDesc.DXILLibrary = ToByteCode(shader);
		libDesc.NumExports = 1;
		libDesc.pExports = &exportDescs.back();
		libraryDescs.push_back(libDesc);
		D3D12_STATE_SUBOBJECT subobject{};
		subobject.Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
		subobject.pDesc = &libraryDescs.back();
		subobjects.push_back(subobject);
		};
	// 各ステージのライブラリを追加
	addLibrary(rayGenShader, rayGenExport);
	addLibrary(missShader, missExport);
	addLibrary(closestHitShader, closestHitExport);

	// ヒットグループの構築
	D3D12_HIT_GROUP_DESC hitGroupDesc{};
	hitGroupDesc.HitGroupExport = hitGroupExport.c_str();
	hitGroupDesc.ClosestHitShaderImport = closestHitExport.c_str();
	hitGroupDesc.AnyHitShaderImport = anyHitExport.empty() ? nullptr : anyHitExport.c_str();
	hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
	D3D12_STATE_SUBOBJECT hitGroupSubobject{};
	hitGroupSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
	hitGroupSubobject.pDesc = &hitGroupDesc;
	subobjects.push_back(hitGroupSubobject);

	// グローバルルートシグネチャのサブオブジェクトを追加
	D3D12_GLOBAL_ROOT_SIGNATURE globalRootSignatureDesc{ globalRootSignature_.Get() };
	D3D12_STATE_SUBOBJECT globalRootSignatureSubobject{};
	globalRootSignatureSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
	globalRootSignatureSubobject.pDesc = &globalRootSignatureDesc;
	subobjects.push_back(globalRootSignatureSubobject);

	// シェーダーコンフィグの構築
	D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
	shaderConfig.MaxPayloadSizeInBytes = variant.maxPayloadSizeInBytes;
	shaderConfig.MaxAttributeSizeInBytes = variant.maxAttributeSizeInBytes;
	D3D12_STATE_SUBOBJECT shaderConfigSubobject{};
	shaderConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
	shaderConfigSubobject.pDesc = &shaderConfig;
	subobjects.push_back(shaderConfigSubobject);
	const D3D12_STATE_SUBOBJECT* shaderConfigSubobjectPtr = &subobjects.back();

	// シェーダーコンフィグを関連付けるエクスポートのリストを構築
	LPCWSTR shaderConfigExports[] = {
		rayGenExport.c_str(),
		missExport.c_str(),
		hitGroupExport.c_str(),
	};
	// シェーダーコンフィグとエクスポートの関連付けを追加
	D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION shaderConfigAssociation{};
	shaderConfigAssociation.pSubobjectToAssociate = shaderConfigSubobjectPtr;
	shaderConfigAssociation.NumExports = _countof(shaderConfigExports);
	shaderConfigAssociation.pExports = shaderConfigExports;
	D3D12_STATE_SUBOBJECT shaderConfigAssociationSubobject{};
	shaderConfigAssociationSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
	shaderConfigAssociationSubobject.pDesc = &shaderConfigAssociation;
	subobjects.push_back(shaderConfigAssociationSubobject);

	// パイプラインコンフィグの構築
	D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
	pipelineConfig.MaxTraceRecursionDepth = variant.maxRecursionDepth;
	D3D12_STATE_SUBOBJECT pipelineConfigSubobject{};
	pipelineConfigSubobject.Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
	pipelineConfigSubobject.pDesc = &pipelineConfig;
	subobjects.push_back(pipelineConfigSubobject);

	D3D12_STATE_OBJECT_DESC desc{};
	desc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
	desc.NumSubobjects = static_cast<UINT>(subobjects.size());
	desc.pSubobjects = subobjects.data();

	// サブオブジェクトをまとめてパイプラインステートオブジェクトを作成
	HRESULT hr = device->CreateStateObject(&desc, IID_PPV_ARGS(&stateObject_));
	Assert::Call(SUCCEEDED(hr), "Failed to create raytracing state object");
	hr = stateObject_->QueryInterface(IID_PPV_ARGS(&stateProps_));
	Assert::Call(SUCCEEDED(hr), "Failed to query ID3D12StateObjectProperties");

	// シェーダーテーブルの構築
	BuildShaderTable(device, rayGenExport, missExport, hitGroupExport);
}

void Engine::RaytracingPipelineState::BuildShaderTable(ID3D12Device8* device,
	const std::wstring& rayGenExport, const std::wstring& missExport, const std::wstring& hitGroupExport) {

	// シェーダーテーブルのサイズは、各レコードがテーブルアライメントに従って配置されるように計算
	shaderTableSize_ = static_cast<UINT>(3 * kTableAlign);

	CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(shaderTableSize_);
	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
	// シェーダーテーブル用のバッファを作成
	HRESULT hr = device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
		&bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&shaderTable_));
	Assert::Call(SUCCEEDED(hr), "Failed to create raytracing shader table");

	uint8_t* mapped = nullptr;
	shaderTable_->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
	std::memset(mapped, 0, shaderTableSize_);

	// シェーダーテーブルの各レコードに対応するシェーダーの識別子をコピー
	std::memcpy(mapped + kRayGenOffset, stateProps_->GetShaderIdentifier(rayGenExport.c_str()), kHandleSize);
	std::memcpy(mapped + kMissOffset, stateProps_->GetShaderIdentifier(missExport.c_str()), kHandleSize);
	std::memcpy(mapped + kHitGroupOffset, stateProps_->GetShaderIdentifier(hitGroupExport.c_str()), kHandleSize);

	shaderTable_->Unmap(0, nullptr);
}
