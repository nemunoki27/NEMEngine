#include "AutoRootSignatureBuilder.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>
#include <Engine/Logger/Logger.h>
#include <Engine/Utility/Algorithm/Algorithm.h>
#include <Engine/Utility/Enum/EnumAdapter.h>

//============================================================================
//	AutoRootSignatureBuilder classMethods
//============================================================================

namespace {

	// ルート引数のキー
	struct BindingKey {

		// リソースの種類
		ShaderBindingKind kind;
		UINT space;
		UINT bindPoint;

		// キーの比較
		bool operator==(const BindingKey& rhs) const {
			return kind == rhs.kind && space == rhs.space && bindPoint == rhs.bindPoint;
		}
	};
	// BindingKeyのハッシュ関数
	struct BindingKeyHash {
		size_t operator()(const BindingKey& key) const {
			size_t h0 = std::hash<int>()(static_cast<int>(key.kind));
			size_t h1 = std::hash<UINT>()(key.space);
			size_t h2 = std::hash<UINT>()(key.bindPoint);
			return h0 ^ (h1 << 1) ^ (h2 << 2);
		}
	};
	// サンプラーリソースが静的サンプラーでカバーされているか
	bool IsCoveredByStaticSampler(const ShaderResourceBinding& resource,
		const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers) {

		if (resource.kind != ShaderBindingKind::Sampler) {
			return false;
		}

		for (const auto& staticSampler : staticSamplers) {
			if (staticSampler.ShaderRegister == resource.bindPoint &&
				staticSampler.RegisterSpace == resource.space) {
				return true;
			}
		}
		return false;
	}
	// 種類からD3D12_DESCRIPTOR_RANGEへ変換
	D3D12_DESCRIPTOR_RANGE_TYPE ToRangeType(ShaderBindingKind kind) {

		switch (kind) {
		case ShaderBindingKind::CBV:     return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		case ShaderBindingKind::SRV:     return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		case ShaderBindingKind::UAV:     return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
		case ShaderBindingKind::Sampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		case ShaderBindingKind::AccelStruct: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		}
		// 未対応
		Assert::Call(false, "Unsupported binding kind for root signature");
		return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	}
	// パイプラインの種類とステージマスクからD3D12_SHADER_VISIBILITYを決定する
	D3D12_SHADER_VISIBILITY ToVisibility(PipelineType pipelineType, ShaderStage stageMask) {

		// コンピュートパイプラインは常にALL
		if (pipelineType == PipelineType::Compute) {
			return D3D12_SHADER_VISIBILITY_ALL;
		}

		// ステージマスクからどのシェーダーステージが使用されているかを判定
		bool hasVS = Algorithm::HasFlag<ShaderStage>(stageMask, ShaderStage::VS);
		bool hasGS = Algorithm::HasFlag<ShaderStage>(stageMask, ShaderStage::GS);
		bool hasPS = Algorithm::HasFlag<ShaderStage>(stageMask, ShaderStage::PS);
		bool hasAS = Algorithm::HasFlag<ShaderStage>(stageMask, ShaderStage::AS);
		bool hasMS = Algorithm::HasFlag<ShaderStage>(stageMask, ShaderStage::MS);
		bool hasCS = Algorithm::HasFlag<ShaderStage>(stageMask, ShaderStage::CS);

		UINT count = 0;
		count += hasVS ? 1 : 0;
		count += hasGS ? 1 : 0;
		count += hasPS ? 1 : 0;
		count += hasAS ? 1 : 0;
		count += hasMS ? 1 : 0;
		count += hasCS ? 1 : 0;

		// 複数ステージから見える場合はALL
		if (count != 1) {
			return D3D12_SHADER_VISIBILITY_ALL;
		}

		if (hasPS) { return D3D12_SHADER_VISIBILITY_PIXEL; }
		if (hasVS) { return D3D12_SHADER_VISIBILITY_VERTEX; }
		if (hasGS) { return D3D12_SHADER_VISIBILITY_GEOMETRY; }
		if (hasMS) { return D3D12_SHADER_VISIBILITY_MESH; }
		if (hasAS) { return D3D12_SHADER_VISIBILITY_AMPLIFICATION; }
		return D3D12_SHADER_VISIBILITY_ALL;
	}

	D3D12_ROOT_SIGNATURE_FLAGS BuildRootSignatureFlags(PipelineType pipelineType, ShaderStage usedStageMask) {

		// パイプラインの種類と使用されているシェーダーステージに応じてルートシグネチャフラグを構築する
		D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		// IA入力レイアウトを使用するパイプラインの場合はフラグを追加
		if (pipelineType == PipelineType::Vertex || pipelineType == PipelineType::Geometry) {

			flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		}
		flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		auto denyIfUnused = [&](ShaderStage stage, D3D12_ROOT_SIGNATURE_FLAGS denyFlag) {
			if (!Algorithm::HasFlag<ShaderStage>(usedStageMask, stage)) {
				flags |= denyFlag;
			}
			};
		denyIfUnused(ShaderStage::VS, D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS);
		denyIfUnused(ShaderStage::PS, D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS);
		denyIfUnused(ShaderStage::GS, D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);
		denyIfUnused(ShaderStage::AS, D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS);
		denyIfUnused(ShaderStage::MS, D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS);
		return flags;
	}
	// ルートディスクリプタを使用できるか
	bool CanUseRootDescriptor(const RootBindingLocation& binding) {

		// 配列や複数ディスクリプタはテーブル
		if (binding.bindCount != 1) {
			return false;
		}
		if (binding.kind == ShaderBindingKind::Sampler) {
			return false;
		}

		switch (binding.kind) {
		case ShaderBindingKind::CBV:
		case ShaderBindingKind::AccelStruct:
			return true;
		case ShaderBindingKind::SRV:

			return binding.rawType == D3D_SIT_STRUCTURED || binding.rawType == D3D_SIT_BYTEADDRESS;
		case ShaderBindingKind::UAV:

			return binding.rawType == D3D_SIT_UAV_RWSTRUCTURED || binding.rawType == D3D_SIT_UAV_RWBYTEADDRESS;
		}
		return false;
	}
	// ルート引数の種類を決定する
	D3D12_ROOT_PARAMETER_TYPE DecideRootParameterType(const RootBindingLocation& binding) {

		// ルートディスクリプタを使用できない場合はテーブル
		if (!CanUseRootDescriptor(binding)) {
			return D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		}
		// ルートディスクリプタを使用できる場合は種類に応じて決定
		switch (binding.kind) {
		case ShaderBindingKind::CBV:
			return D3D12_ROOT_PARAMETER_TYPE_CBV;
		case ShaderBindingKind::SRV:
		case ShaderBindingKind::AccelStruct:
			return D3D12_ROOT_PARAMETER_TYPE_SRV;
		case ShaderBindingKind::UAV:
			return D3D12_ROOT_PARAMETER_TYPE_UAV;
		}
		return {};
	}
}

RootSignatureBuildResult AutoRootSignatureBuilder::Build(ID3D12Device* device, PipelineType pipelineType,
	const std::vector<const CompiledShader*>& shaders, const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers) {

	RootSignatureBuildResult result{};

	//============================================================================
	//	各シェーダーのリフレクションからリソースバインディングをマージ
	//============================================================================
	std::unordered_map<BindingKey, RootBindingLocation, BindingKeyHash> merged{};
	ShaderStage usedStageMask = ShaderStage::None;
	for (const CompiledShader* shader : shaders) {

		// シェーダーオブジェクトがないものは無視
		if (!shader || !shader->object) {
			continue;
		}

		usedStageMask |= shader->stage;
		for (const auto& resource : shader->reflection.resources) {

			// 静的サンプラーでカバーされているものはルートシグネチャに含めない
			if (IsCoveredByStaticSampler(resource, staticSamplers)) {
				continue;
			}

			BindingKey key{ resource.kind, resource.space, resource.bindPoint };
			auto it = merged.find(key);
			if (it == merged.end()) {

				// ルート引数の場所を新規に追加
				RootBindingLocation location{};
				location.name = resource.name;
				location.kind = resource.kind;
				location.bindPoint = resource.bindPoint;
				location.bindCount = resource.bindCount;
				location.space = resource.space;
				location.stageMask = resource.stageMask;
				location.rawType = resource.rawType;
				merged.emplace(key, std::move(location));
			} else {

				Assert::Call(it->second.bindCount == resource.bindCount, "Mismatched bindCount on same register/space");
				// 共有されるステージ情報をマージ
				it->second.stageMask |= resource.stageMask;
			}
		}
	}

	// ルート引き数の順番を安定させるためにソート
	for (auto& [key, value] : merged) {

		result.bindings.emplace_back(std::move(value));
	}
	std::sort(result.bindings.begin(), result.bindings.end(), [](
		const RootBindingLocation& rootA, const RootBindingLocation& rootB) {
			if (rootA.space != rootB.space) {
				return rootA.space < rootB.space;
			}
			if (rootA.kind != rootB.kind) {
				return static_cast<int32_t>(rootA.kind) < static_cast<int32_t>(rootB.kind);
			}
			return rootA.bindPoint < rootB.bindPoint;
		});

	// パラメータの種類を決定
	std::vector<D3D12_ROOT_PARAMETER_TYPE> parameterTypes(result.bindings.size());
	size_t tableCount = 0;
	for (size_t i = 0; i < result.bindings.size(); ++i) {

		parameterTypes[i] = DecideRootParameterType(result.bindings[i]);
		if (parameterTypes[i] == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
			++tableCount;
		}
	}

	// レンジ、ルート引数の配列を構築
	std::vector<D3D12_DESCRIPTOR_RANGE1> ranges(tableCount);
	std::vector<D3D12_ROOT_PARAMETER1> params(result.bindings.size());
	size_t tableIndex = 0;
	for (size_t i = 0; i < result.bindings.size(); ++i) {

		auto& binding = result.bindings[i];
		auto& param = params[i];

		// ログ出力
		Logger::Output(LogType::Engine,"RootParam[{}]: name={},kind={}",
			i, binding.name, EnumAdapter<ShaderBindingKind>::ToString(binding.kind));

		binding.rootParameterIndex = static_cast<UINT>(i);
		binding.parameterType = parameterTypes[i];
		param.ShaderVisibility = ToVisibility(pipelineType, binding.stageMask);

		// ルート引数の種類に応じて構築
		switch (binding.parameterType) {
		case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE: {

			auto& range = ranges[tableIndex++];
			range.RangeType = ToRangeType(binding.kind);
			range.NumDescriptors = binding.bindCount;
			range.BaseShaderRegister = binding.bindPoint;
			range.RegisterSpace = binding.space;
			range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
			range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
			if (binding.kind != ShaderBindingKind::Sampler) {
				range.Flags |= D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE;
			}
			param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			param.DescriptorTable.NumDescriptorRanges = 1;
			param.DescriptorTable.pDescriptorRanges = &range;
			break;
		}
		case D3D12_ROOT_PARAMETER_TYPE_CBV:
		case D3D12_ROOT_PARAMETER_TYPE_SRV:
		case D3D12_ROOT_PARAMETER_TYPE_UAV: {

			param.ParameterType = binding.parameterType;
			param.Descriptor.ShaderRegister = binding.bindPoint;
			param.Descriptor.RegisterSpace = binding.space;
			param.Descriptor.Flags = D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE;
			break;
		}
		default:
			Assert::Call(false, "Unsupported root parameter type");
			break;
		}
	}

	// ルートシグネイチャを構築
	D3D12_VERSIONED_ROOT_SIGNATURE_DESC signatureDesc{};
	signatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	signatureDesc.Desc_1_1.NumParameters = static_cast<UINT>(params.size());
	signatureDesc.Desc_1_1.pParameters = params.empty() ? nullptr : params.data();
	signatureDesc.Desc_1_1.NumStaticSamplers = static_cast<UINT>(staticSamplers.size());
	signatureDesc.Desc_1_1.pStaticSamplers = staticSamplers.empty() ? nullptr : staticSamplers.data();
	signatureDesc.Desc_1_1.Flags = BuildRootSignatureFlags(pipelineType, usedStageMask);
	ComPtr<ID3DBlob> signatureBlob = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeVersionedRootSignature(&signatureDesc, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		if (errorBlob) {
			const char* err = reinterpret_cast<const char*>(errorBlob->GetBufferPointer());
			Assert::Call(false, std::string("D3D12SerializeVersionedRootSignature failed: ") + err);
		} else {
			Assert::Call(false, "D3D12SerializeVersionedRootSignature failed");
		}
	}
	// デバイスからルートシグネチャを生成
	hr = device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(), IID_PPV_ARGS(&result.rootSignature));
	Assert::Call(SUCCEEDED(hr), "CreateRootSignature failed");
	return result;
}