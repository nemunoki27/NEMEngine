#include "MeshSubMeshPicker.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

#include <unordered_set>

//============================================================================
//	MeshSubMeshPicker classMethods
//============================================================================
namespace {

	void CreatePickTarget(Engine::GraphicsCore& graphicsCore, Engine::MultiRenderTarget& target, uint32_t width, uint32_t height) {

		using namespace Engine;
		MultiRenderTargetCreateDesc desc{};
		desc.width = width;
		desc.height = height;
		desc.colors.emplace_back(ColorAttachmentDesc{
			.name = "MeshPickingID",
			.format = DXGI_FORMAT_R32G32B32A32_UINT,
			.clearColor = Color4(0, 0, 0, 0),
			.createUAV = false,
			});

		auto depth = DepthTextureCreateDesc::View();
		depth.width = width;
		depth.height = height;
		depth.debugName = L"MeshPickingDepth";
		desc.depth = depth;

		target.Create(graphicsCore.GetDXObject().GetDevice(),
			&graphicsCore.GetRTVDescriptor(), &graphicsCore.GetDSVDescriptor(),
			&graphicsCore.GetSRVDescriptor(), desc);
	}
}

void Engine::MeshSubMeshPicker::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	CreatePickTarget(graphicsCore, renderTarget_, 1, 1);
	readbackSlots_.Connect(graphicsCore.GetDXObject().GetResourceRetirement());

	initialized_ = true;
}

Engine::MultiRenderTarget* Engine::MeshSubMeshPicker::GetRegionTarget(
	GraphicsCore& graphicsCore, uint32_t width, uint32_t height) {

	if (!initialized_ || !width || !height) return nullptr;
	auto& slot = readbackSlots_[graphicsCore.GetDXObject().GetDxCommand()->GetCurrentFrameIndex()];
	if (slot.pending) return nullptr;
	slot.regionTarget = std::make_unique<MultiRenderTarget>();
	CreatePickTarget(graphicsCore, *slot.regionTarget, width, height);
	return slot.regionTarget.get();
}

void Engine::MeshSubMeshPicker::Finalize() {

	renderTarget_.Destroy();
	for (ReadbackSlot& slot : readbackSlots_) {
		slot.requestID = 0;
		slot.fenceValue = 0;
		slot.pending = false;
		slot.regionTarget.reset();
		slot.regionBuffer.reset();
	}
	initialized_ = false;
}

Engine::MeshSubMeshPickOutcome
Engine::MeshSubMeshPicker::ConsumePendingResult(
	GraphicsCore& graphicsCore, ECSWorld* world) {

	MeshSubMeshPickOutcome outcome{};
	const uint64_t completedFence =
		graphicsCore.GetDXObject().GetCommandQueue()->
			GetCompletedFenceValue();
	for (ReadbackSlot& slot : readbackSlots_) {

		if (!slot.pending || completedFence < slot.fenceValue) {
			continue;
		}
		slot.pending = false;
		slot.regionTarget.reset();
		auto regionBuffer = std::move(slot.regionBuffer);
		if (outcome.resolved && slot.requestID < outcome.requestID) continue;

		outcome = {};
		outcome.resolved = true;
		outcome.requestID = slot.requestID;
		outcome.region = slot.region;
		if (!world) continue;

		if (slot.region) {
			std::unordered_set<uint32_t> seen;
			for (uint32_t y = 0; y < slot.height; ++y) {
				for (uint32_t x = 0; x < slot.width; ++x) {
					const auto& pixel = regionBuffer->GetReadbackData(static_cast<size_t>(y) * slot.rowLength + x);
					const Entity entity{pixel.entityIndex, pixel.entityGeneration};
					if (pixel.valid && !seen.contains(entity.index) && world->IsAlive(entity)) {
						seen.insert(entity.index);
						outcome.entities.push_back(entity);
					}
				}
			}
			continue;
		}
		const PickResult& result = slot.clickBuffer.GetReadbackData();
		if (result.valid == 0 ||
			result.entityIndex == UINT32_MAX ||
			result.entityGeneration == UINT32_MAX) {
			continue;
		}

		const Entity entity{
			result.entityIndex, result.entityGeneration };
		if (!world->IsAlive(entity)) {
			continue;
		}

		outcome.hit = true;
		outcome.entity = entity;
		outcome.subMeshIndex = result.subMeshIndex;
		const std::span<const SubMeshMaterial> subMeshes =
			GetMeshSubMeshes(*world, entity);
		if (result.subMeshIndex < subMeshes.size()) {
			outcome.subMeshStableID =
				subMeshes[result.subMeshIndex].stableID;
		}
	}
	return outcome;
}

bool Engine::MeshSubMeshPicker::ExecuteReadback(
	GraphicsCore& graphicsCore, uint64_t requestID, MultiRenderTarget& target, bool region) {

	if (!initialized_ || !target.IsValid() || requestID == 0) {
		return false;
	}

	RenderTexture2D* sourceTexture = target.GetColorTexture(0);
	if (!sourceTexture || !sourceTexture->GetResource()) {
		return false;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ReadbackSlot& slot = readbackSlots_[
		dxCommand->GetCurrentFrameIndex() %
		kGraphicsFrameContextCount];
	if (slot.pending) {
		return false;
	}
	slot.width = target.GetWidth();
	slot.height = target.GetHeight();
	slot.region = region;
	const uint32_t rowPitch = (static_cast<uint32_t>(sizeof(PickResult)) * slot.width +
		D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
	slot.rowLength = rowPitch / sizeof(PickResult);
	if (region) slot.regionBuffer = std::make_unique<DxReadbackBuffer<PickResult>>();
	auto& buffer = region ? *slot.regionBuffer : slot.clickBuffer;
	if (!buffer.GetResource()) {
		buffer.CreateBuffer(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(), static_cast<size_t>(slot.rowLength) * slot.height);
	}
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	sourceTexture->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_SOURCE);

	D3D12_TEXTURE_COPY_LOCATION destination{};
	destination.pResource = buffer.GetResource();
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	destination.PlacedFootprint.Footprint.Format =
		DXGI_FORMAT_R32G32B32A32_UINT;
	destination.PlacedFootprint.Footprint.Width = slot.width;
	destination.PlacedFootprint.Footprint.Height = slot.height;
	destination.PlacedFootprint.Footprint.Depth = 1;
	destination.PlacedFootprint.Footprint.RowPitch = rowPitch;

	D3D12_TEXTURE_COPY_LOCATION source{};
	source.pResource = sourceTexture->GetResource();
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	source.SubresourceIndex = 0;

	commandList->CopyTextureRegion(
		&destination, 0, 0, 0, &source, nullptr);
	slot.requestID = requestID;
	slot.fenceValue =
		graphicsCore.GetDXObject().GetCommandQueue()->
			GetLastSignaledFenceValue() + 1;
	slot.pending = true;
	return true;
}
