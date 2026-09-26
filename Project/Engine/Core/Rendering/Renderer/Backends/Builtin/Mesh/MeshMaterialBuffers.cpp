#include "MeshMaterialBuffers.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

namespace {
	uint64_t ComputeMaterialLayoutHash(
		const Engine::MaterialParameterLayout& layout) {

		uint64_t hash = 1469598103934665603ull;
		Engine::Algorithm::HashCombine(hash, layout.GetSizeInBytes());
		for (const Engine::ShaderConstantBufferVariable& variable :
			layout.GetVariables()) {

			Engine::Algorithm::HashCombine(hash,
				static_cast<uint64_t>(std::hash<std::string>{}(variable.name)));
			Engine::Algorithm::HashCombine(hash, variable.offset);
			Engine::Algorithm::HashCombine(hash, variable.size);
			Engine::Algorithm::HashCombine(hash,
				static_cast<uint64_t>(variable.valueClass));
			Engine::Algorithm::HashCombine(hash,
				static_cast<uint64_t>(variable.valueType));
		}
		return hash;
	}
}

Engine::MeshMaterialBuffers::SubMeshMaterialParamBuffer&
Engine::MeshMaterialBuffers::GetSubMeshMaterialParamBuffer(
	MaterialPassKind passKind) {

	size_t index = static_cast<size_t>(passKind);
	if (kSubMeshMaterialPassBufferCount <= index) {
		index = static_cast<size_t>(MaterialPassKind::Invalid);
	}
	auto& buffer = subMeshParamBuffers_[index];
	if (!buffer) {
		buffer = std::make_unique<SubMeshMaterialParamBuffer>();
	}
	return *buffer;
}

void Engine::MeshMaterialBuffers::ReleaseSubMeshMaterialParamBuffer(
	SubMeshMaterialParamBuffer& buffer, SRVDescriptor* srvDescriptor) {

	if (srvDescriptor) {
		for (uint32_t& index : buffer.srvIndices) {

			if (index != UINT32_MAX) {
				srvDescriptor->Retire(index, {});
				index = UINT32_MAX;
			}
		}
	}
	buffer.buffer.Release();
	buffer.handles = {};
	buffer.packedScratch.clear();
	buffer.sourceGenerations.clear();
	buffer.packedSourceGeneration = 0;
	buffer.elementGenerations.clear();
	buffer.uploadedElements = {};
	buffer.layoutHash = 0;
	buffer.materialHash = 0;
	buffer.material = nullptr;
	buffer.dataGeneration = 1;
	buffer.uploadedGenerations = { 0, 0, 0 };
	buffer.stride = 0;
	buffer.available = false;
	buffer.dirty = true;
}

void Engine::MeshMaterialBuffers::UploadSubMeshMaterialParams(const MaterialAsset* material,
	const MaterialParameterLayout& layout, const RenderDrawContext& drawContext, ID3D12Device* device, SRVDescriptor* srvDescriptor,
	std::span<const MaterialParameterSet> parameters, std::span<const uint64_t> generations,
	uint64_t parameterGeneration, bool& usesFallbackTexture) {

	FrameProfiler::ScopedSample total(FrameProfiler::Category::MeshBatchUpload);
	// シェーダーがMaterialParameters構造化バッファを宣言していないバッチはここで早期に無効化する
	activeSubMeshParamBuffer_ = nullptr;
	if (!layout.IsValid() || parameters.empty() || !device || !srvDescriptor) {
		return;
	}
	SubMeshMaterialParamBuffer& buffer =
		GetSubMeshMaterialParamBuffer(drawContext.passKind);
	buffer.available = false;

	bool usedFallbackTexture = false;

	// テクスチャSemanticからsRGB可否を決めてbindless indexへ解決する
	// 未指定はkNoTextureを返しシェーダー側でテクスチャなしの分岐に乗せる
	auto resolveTexture = [&](MaterialParameterSemantic semantic,
		const AssetID& id) {

		const MaterialParameterBufferBuilder::TextureResolveResult result =
			BackendDrawCommon::ResolveMaterialTextureIndex(
				drawContext, semantic, id);
		usedFallbackTexture |= !result.cacheable;
		return result;
		};

	const MaterialParameterSet emptyMap{};
	const MaterialParameterSet& defaults =
		material ? material->parameters : emptyMap;

	// リフレクションから取得した構造体strideをそのまま使用する
	const uint32_t stride = layout.GetSizeInBytes();
	const uint32_t elementCount = static_cast<uint32_t>(parameters.size());
	const uint64_t layoutHash = ComputeMaterialLayoutHash(layout);
	const uint64_t materialHash = material ?
		material->parameters.GetContentHash() : 0;
	const uint64_t textureRevision = drawContext.graphicsCore->GetTextureUploadService().GetContentRevision();
	const bool rebuildPacked = buffer.textureRevision != textureRevision || buffer.dirty || buffer.layoutHash != layoutHash ||
		buffer.materialHash != materialHash || buffer.material != material ||
		buffer.sourceGenerations.size() != elementCount;
	if (rebuildPacked || buffer.packedSourceGeneration != parameterGeneration) {
		FrameProfiler::ScopedSample build(FrameProfiler::Category::MeshMaterialBuild);
		if (rebuildPacked) {
			buffer.packedScratch.assign(static_cast<size_t>(stride) * elementCount, 0);
			buffer.sourceGenerations.assign(elementCount, 0);
			buffer.elementGenerations.resize(elementCount);
		}
		bool changed = false;
		for (uint32_t i = 0; i < elementCount; ++i) {
			if (!rebuildPacked && buffer.sourceGenerations[i] == generations[i]) { continue; }
			const size_t offset = static_cast<size_t>(stride) * i;
			// 小さいstrideもBuilderの最小領域を満たし、要素ごとのヒープ確保を避ける
			std::array<uint8_t, 16> smallElement{};
			const std::span<uint8_t> destination(buffer.packedScratch.data() + offset, stride);
			const std::span<uint8_t> element = stride < smallElement.size() ?
				std::span<uint8_t>(smallElement) : destination;
			MaterialParameterBufferBuilder::BuildElementInto(
				element, defaults, parameters[i], layout, resolveTexture);
			if (stride < smallElement.size()) {
				std::memcpy(destination.data(), element.data(), stride);
			}
			buffer.sourceGenerations[i] = generations[i];
			buffer.elementGenerations[i] = ++buffer.dataGeneration;
			changed = true;
		}
		if (changed) {
			buffer.layoutHash = layoutHash;
			buffer.materialHash = materialHash;
			buffer.textureRevision = textureRevision;
			buffer.material = material;
			// 読込中のテクスチャだけは次回も再解決する
			buffer.dirty = usedFallbackTexture;
			usesFallbackTexture |= usedFallbackTexture;
		}
		buffer.packedSourceGeneration = parameterGeneration;
	}

	// 容量不足時は全フレーム分を拡張し、stride変更時はSRVだけを更新する
	const uint32_t requiredBytes =
		static_cast<uint32_t>(buffer.packedScratch.size());
	const std::string resourceName =
		"gMeshSubMeshMaterialParameters[" +
		std::to_string(static_cast<uint32_t>(drawContext.passKind)) + "]";
	buffer.buffer.SetRetirementQueue(srvDescriptor->GetRetirementQueue());
	const bool reallocated = buffer.buffer.EnsureCapacity(
		device, requiredBytes, resourceName, 4096);
	if (reallocated || stride != buffer.stride ||
		buffer.srvIndices[0] == UINT32_MAX) {

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = (std::max)(
			static_cast<uint32_t>(buffer.buffer.GetCapacity()) / stride, 1u);
		srvDesc.Buffer.StructureByteStride = stride;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		for (uint32_t frameIndex = 0;
			frameIndex < kGraphicsFrameContextCount; ++frameIndex) {

			if (buffer.srvIndices[frameIndex] != UINT32_MAX) {
				srvDescriptor->Retire(buffer.srvIndices[frameIndex], {});
				buffer.srvIndices[frameIndex] = UINT32_MAX;
			}
			srvDescriptor->CreateSRV(
				buffer.srvIndices[frameIndex],
				buffer.buffer.GetResource(frameIndex), srvDesc);
			buffer.handles[frameIndex] =
				srvDescriptor->GetGPUHandle(
					buffer.srvIndices[frameIndex]);
		}
		buffer.stride = stride;
		buffer.uploadedGenerations = { 0, 0, 0 };
		buffer.uploadedElements = {};
	}

	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	if (!buffer.packedScratch.empty() &&
		buffer.uploadedGenerations[frameIndex] !=
			buffer.dataGeneration) {

		FrameProfiler::ScopedSample transfer(FrameProfiler::Category::MeshBufferTransfer);
		auto& uploaded = buffer.uploadedElements[frameIndex];
		uploaded.resize(elementCount, 0);
		uint32_t first = 0;
		while (first < elementCount) {
			if (uploaded[first] == buffer.elementGenerations[first]) { ++first; continue; }
			uint32_t end = first + 1;
			while (end < elementCount && uploaded[end] != buffer.elementGenerations[end]) { ++end; }
			const size_t offset = static_cast<size_t>(first) * stride;
			const size_t bytes = static_cast<size_t>(end - first) * stride;
			buffer.buffer.Write(buffer.packedScratch.data() + offset, bytes, offset);
			FrameProfiler::GetInstance().AddMeshTransferBytes(bytes);
			for (uint32_t i = first; i < end; ++i) { uploaded[i] = buffer.elementGenerations[i]; }
			first = end;
		}
		buffer.uploadedGenerations[frameIndex] =
			buffer.dataGeneration;
	}
	buffer.available = true;
	activeSubMeshParamBuffer_ = &buffer;
}

void Engine::MeshMaterialBuffers::Release(SRVDescriptor* srvDescriptor) {

	for (auto& buffer : subMeshParamBuffers_) {

		if (buffer) {
			ReleaseSubMeshMaterialParamBuffer(*buffer, srvDescriptor);
			buffer.reset();
		}
	}
	activeSubMeshParamBuffer_ = nullptr;
}

D3D12_GPU_VIRTUAL_ADDRESS Engine::MeshMaterialBuffers::GetGPUAddress() const {

	return activeSubMeshParamBuffer_ ? activeSubMeshParamBuffer_->buffer.GetGPUAddress() : 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE Engine::MeshMaterialBuffers::GetGPUHandle() const {

	return activeSubMeshParamBuffer_ ? activeSubMeshParamBuffer_->handles[GraphicsFrameState::GetCurrentIndex()] : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

void Engine::MeshMaterialBuffers::Invalidate() {

	for (auto& buffer : subMeshParamBuffers_) {
		if (buffer) {
			buffer->dirty = true;
		}
	}
}
