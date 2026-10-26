#include "ParticleDepthSnapshotTests.h"

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include <Engine/Core/Rendering/Particle/ParticleOpaqueDepthSnapshot.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxDepthStencilView.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>

// c++
#include <cmath>
#include <cstring>

bool NEMTests::CheckParticleDepthSnapshot(ParticleGPUFixture& fixture) {

	using namespace Engine;
	DSVDescriptor depths;
	depths.Init(fixture.device.Get(), {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, D3D12_DESCRIPTOR_HEAP_FLAG_NONE});
	depths.SetRetirementQueue(fixture.retirement);
	ParticleOpaqueDepthSnapshot snapshot;
	DepthTexture2D source;
	for (uint32_t frameCount : {1u, 2u, 1u}) {
		fixture.ChangeFrameCount(frameCount);
		for (uint32_t size : {2u, 7u, 2u}) {
			DepthTextureCreateDesc desc;
			desc.width = desc.height = size;
			desc.resourceFormat = DXGI_FORMAT_R32_TYPELESS;
			desc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
			desc.srvFormat = DXGI_FORMAT_R32_FLOAT;
			source.Create(&depths, &fixture.descriptors, desc);
			fixture.commands.GetCommandList()->ClearDepthStencilView(source.GetDSVCPUHandle(), D3D12_CLEAR_FLAG_DEPTH, 0.25f, 0, 0, nullptr);
			auto* result = snapshot.Copy(fixture.commands, depths, fixture.descriptors, source);
			if (!result || result == &source || result->GetWidth() != size || result->GetSRVGPUHandle().ptr == 0 ||
				result != snapshot.Copy(fixture.commands, depths, fixture.descriptors, source)) return false;
			source.Transition(fixture.commands, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			fixture.commands.GetCommandList()->ClearDepthStencilView(source.GetDSVCPUHandle(), D3D12_CLEAR_FLAG_DEPTH, 0.75f, 0, 0, nullptr);
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
			UINT64 bytes = 0;
			const auto resourceDesc = result->GetResource()->GetDesc();
			fixture.device.Get()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
			ComPtr<ID3D12Resource> readback;
			DxUtils::CreateReadbackBufferResource(fixture.device.Get(), readback, bytes);
			result->Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			D3D12_TEXTURE_COPY_LOCATION dest{readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
			dest.PlacedFootprint = footprint;
			D3D12_TEXTURE_COPY_LOCATION src{result->GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
			fixture.commands.GetCommandList()->CopyTextureRegion(&dest, 0, 0, 0, &src, nullptr);
			fixture.Finish();
			void* mapped = nullptr;
			const D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
			if (FAILED(readback->Map(0, &range, &mapped))) return false;
			float actual = 0;
			std::memcpy(&actual, static_cast<uint8_t*>(mapped) + footprint.Offset, sizeof(actual));
			const D3D12_RANGE empty{};
			readback->Unmap(0, &empty);
			if (std::abs(actual - 0.25f) > 0.00001f) return false;
		}
	}
	snapshot.Destroy();
	source.Destroy();
	fixture.Finish();
	return fixture.CheckMessages();
}
