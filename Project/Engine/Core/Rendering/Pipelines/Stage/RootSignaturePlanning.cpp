#include "RootSignaturePlanning.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <utility>
// directX
#include <d3d12shader.h>

using namespace Engine;

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
	// 静的Samplerとの重なり方
	enum class StaticSamplerCoverage { None, Complete, Partial };

	// 使用ステージから可視範囲を決める
	D3D12_SHADER_VISIBILITY ToVisibility(PipelineType pipelineType, ShaderStage stageMask) {

		// コンピュートパイプラインは常にALL
		if (pipelineType == PipelineType::Compute) {
			return D3D12_SHADER_VISIBILITY_ALL;
		}

		// 使用ステージを数える
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

	D3D12_ROOT_SIGNATURE_FLAGS BuildRootSignatureFlags(
		PipelineType pipelineType, ShaderStage usedStageMask,
		bool usesDirectHeapIndexing) {

		// 使用しないステージのアクセスを禁止する
		D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

		// VertexとGeometryではIA入力を許可する
		if (pipelineType == PipelineType::Vertex || pipelineType == PipelineType::Geometry) {

			flags |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		}
		if (usesDirectHeapIndexing) {
			flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
		}
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
	// 静的Samplerが配列の全要素を覆うか調べる
	StaticSamplerCoverage GetStaticSamplerCoverage(PipelineType pipelineType, const ShaderResourceBinding& resource,
		const std::vector<D3D12_STATIC_SAMPLER_DESC>& samplers) {

		if (resource.kind != ShaderBindingKind::Sampler) {
			return StaticSamplerCoverage::None;
		}
		uint64_t end = resource.bindCount == 0 ? uint64_t{UINT_MAX} + 1 :
			uint64_t{resource.bindPoint} + resource.bindCount;
		// 別ステージのSamplerでは入力を満たさない
		D3D12_SHADER_VISIBILITY visibility = ToVisibility(pipelineType, resource.stageMask);
		auto matchesVisibility = [&](const auto& sampler) {

			return sampler.ShaderVisibility == D3D12_SHADER_VISIBILITY_ALL || sampler.ShaderVisibility == visibility;
		};
		bool overlaps = std::any_of(samplers.begin(), samplers.end(), [&](const auto& sampler) {

			return sampler.RegisterSpace == resource.space && sampler.ShaderRegister >= resource.bindPoint &&
				sampler.ShaderRegister < end && (visibility == D3D12_SHADER_VISIBILITY_ALL || matchesVisibility(sampler));
		});
		if (!overlaps) {
			return StaticSamplerCoverage::None;
		}
		if (resource.bindCount == 0 || resource.bindCount > samplers.size() || end > uint64_t{UINT_MAX} + 1) {
			return StaticSamplerCoverage::Partial;
		}
		for (UINT index = 0; index < resource.bindCount; ++index) {

			if (!std::any_of(samplers.begin(), samplers.end(), [&](const auto& sampler) {

				return sampler.RegisterSpace == resource.space && sampler.ShaderRegister == resource.bindPoint + index &&
					matchesVisibility(sampler);
			})) {
				return StaticSamplerCoverage::Partial;
			}
		}
		return StaticSamplerCoverage::Complete;
	}
	// 未知の種類をGPU生成へ渡さない
	bool IsKnownBindingKind(ShaderBindingKind kind) {

		switch (kind) {
		case ShaderBindingKind::CBV:
		case ShaderBindingKind::SRV:
		case ShaderBindingKind::UAV:
		case ShaderBindingKind::Sampler:
		case ShaderBindingKind::AccelStruct:
			return true;
		}
		return false;
	}
}

//============================================================================
//	RootSignaturePlanning functions
//============================================================================
Engine::RootSignaturePlan Engine::RootSignaturePlanning::Build(PipelineType pipelineType,
	const std::vector<const CompiledShader*>& shaders, const std::vector<D3D12_STATIC_SAMPLER_DESC>& staticSamplers) {

	RootSignaturePlan plan{};
	plan.staticSamplers = staticSamplers;
	std::unordered_map<BindingKey, RootBindingLocation, BindingKeyHash> merged;
	ShaderStage usedStageMask = ShaderStage::None;
	bool usesDirectHeapIndexing = false;

	// 同じRegisterとSpaceの入力を統合する
	for (const CompiledShader* shader : shaders) {

		if (!shader || !shader->IsValid()) {
			continue;
		}
		usedStageMask |= shader->stage;
		usesDirectHeapIndexing |=
			(shader->reflection.requiresFlags & D3D_SHADER_REQUIRES_RESOURCE_DESCRIPTOR_HEAP_INDEXING) != 0;
		for (const ShaderResourceBinding& resource : shader->reflection.resources) {

			if (!IsKnownBindingKind(resource.kind)) {
				plan.diagnostics = "未対応のBinding種別です: " + resource.name;
				return plan;
			}
			StaticSamplerCoverage coverage = GetStaticSamplerCoverage(pipelineType, resource, staticSamplers);
			if (coverage == StaticSamplerCoverage::Complete) {
				continue;
			}
			if (coverage == StaticSamplerCoverage::Partial) {
				plan.diagnostics = "Samplerの配列または使用ステージの一部だけが静的Samplerに含まれています: " + resource.name;
				return plan;
			}

			BindingKey key{resource.kind, resource.space, resource.bindPoint};
			auto found = merged.find(key);
			if (found == merged.end()) {

				RootBindingLocation binding{};
				binding.name = resource.name;
				binding.kind = resource.kind;
				binding.bindPoint = resource.bindPoint;
				binding.bindCount = resource.bindCount;
				binding.space = resource.space;
				binding.stageMask = resource.stageMask;
				binding.rawType = resource.rawType;
				merged.emplace(key, std::move(binding));
				continue;
			}
			if (found->second.rawType != resource.rawType) {
				plan.diagnostics = "同じRegisterとSpaceに異なる入力型が指定されています: " + resource.name;
				return plan;
			}
			// 非有界配列は統合後も非有界にする
			if (found->second.bindCount == 0 || resource.bindCount == 0) {
				found->second.bindCount = 0;
			} else if (found->second.bindCount != resource.bindCount) {
				plan.diagnostics = "同じRegisterとSpaceに異なるBindCountが指定されています: " + resource.name;
				return plan;
			}
			found->second.stageMask |= resource.stageMask;
		}
	}

	// Register順を固定して配置を決める
	plan.bindings.reserve(merged.size());
	for (auto& [key, binding] : merged) {
		plan.bindings.emplace_back(std::move(binding));
	}
	std::sort(plan.bindings.begin(), plan.bindings.end(), [](const auto& lhs, const auto& rhs) {

		if (lhs.space != rhs.space) {
			return lhs.space < rhs.space;
		}
		if (lhs.kind != rhs.kind) {
			return lhs.kind < rhs.kind;
		}
		return lhs.bindPoint < rhs.bindPoint;
	});
	plan.visibilities.reserve(plan.bindings.size());
	size_t rootCost = 0;
	for (size_t index = 0; index < plan.bindings.size(); ++index) {

		auto& binding = plan.bindings[index];
		binding.rootParameterIndex = static_cast<UINT>(index);
		binding.parameterType = DecideRootParameterType(binding);
		plan.visibilities.push_back(ToVisibility(pipelineType, binding.stageMask));
		rootCost += binding.parameterType == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE ? 1 : 2;
		if (rootCost > D3D12_MAX_ROOT_COST) {
			plan.diagnostics = "Root SignatureのDWORD数が上限を超えています";
			return plan;
		}
	}
	plan.flags = BuildRootSignatureFlags(pipelineType, usedStageMask, usesDirectHeapIndexing);
	return plan;
}
