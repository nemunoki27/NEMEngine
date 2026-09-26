#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Buffers/DxFrameMappedUploadBuffer.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <array>
#include <memory>
#include <span>

namespace Engine {

	class SRVDescriptor;
	struct RenderDrawContext;

	//============================================================================
	//	MeshMaterialBuffers class
	//	Pass別のMaterial転送先と差分を管理する
	//============================================================================
	class MeshMaterialBuffers {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Pass別のパラメータを構築して転送する
		void UploadSubMeshMaterialParams(const MaterialAsset* material, const MaterialParameterLayout& layout,
			const RenderDrawContext& drawContext, ID3D12Device* device, SRVDescriptor* srvDescriptor,
			std::span<const MaterialParameterSet> parameters, std::span<const uint64_t> generations,
			uint64_t parameterGeneration, bool& usesFallbackTexture);
		// 所有するBufferとDescriptorを解放する
		void Release(SRVDescriptor* srvDescriptor);
		// パラメータの再構築を要求する
		void Invalidate();
		void ResetActive() { activeSubMeshParamBuffer_ = nullptr; }
		bool IsAvailable() const { return activeSubMeshParamBuffer_ && activeSubMeshParamBuffer_->available; }
		D3D12_GPU_VIRTUAL_ADDRESS GetGPUAddress() const;
		D3D12_GPU_DESCRIPTOR_HANDLE GetGPUHandle() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		struct SubMeshMaterialParamBuffer {

			DxFrameMappedUploadBuffer buffer{};
			std::array<D3D12_GPU_DESCRIPTOR_HANDLE,
				kGraphicsFrameContextCount> handles{};
			std::array<uint32_t, kGraphicsFrameContextCount>
				srvIndices = { UINT32_MAX, UINT32_MAX, UINT32_MAX };
			std::vector<uint8_t> packedScratch{};
			std::vector<uint64_t> sourceGenerations;
			uint64_t packedSourceGeneration = 0;
			std::vector<uint64_t> elementGenerations;
			std::array<std::vector<uint64_t>, kGraphicsFrameContextCount> uploadedElements;
			uint64_t layoutHash = 0;
			uint64_t materialHash = 0;
			uint64_t textureRevision = 0;
			const MaterialAsset* material = nullptr;
			uint64_t dataGeneration = 1;
			std::array<uint64_t, kGraphicsFrameContextCount>
				uploadedGenerations = { 0, 0, 0 };
			uint32_t stride = 0;
			bool available = false;
			bool dirty = true;
		};

		static constexpr size_t kSubMeshMaterialPassBufferCount =
			static_cast<size_t>(MaterialPassKind::RayTracing) + 1;
		std::array<std::unique_ptr<SubMeshMaterialParamBuffer>,
			kSubMeshMaterialPassBufferCount> subMeshParamBuffers_{};
		SubMeshMaterialParamBuffer* activeSubMeshParamBuffer_ = nullptr;

		// PassのBufferを取得する
		SubMeshMaterialParamBuffer& GetSubMeshMaterialParamBuffer(MaterialPassKind passKind);
		// BufferとDescriptorを解放する
		void ReleaseSubMeshMaterialParamBuffer(SubMeshMaterialParamBuffer& buffer, SRVDescriptor* srvDescriptor);
	};
}
