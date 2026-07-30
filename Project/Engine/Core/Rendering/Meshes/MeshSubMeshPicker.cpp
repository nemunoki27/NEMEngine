#include "MeshSubMeshPicker.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	MeshSubMeshPicker classMethods
//============================================================================
void Engine::MeshSubMeshPicker::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	MultiRenderTargetCreateDesc desc{};
	desc.width = 1;
	desc.height = 1;
	desc.colors.emplace_back(ColorAttachmentDesc{
		.name = "MeshPickingID",
		.format = DXGI_FORMAT_R32G32B32A32_UINT,
		.clearColor = Color4::Black(),
		.createUAV = false,
		});

	DepthTextureCreateDesc depth{};
	depth.width = 1;
	depth.height = 1;
	depth.resourceFormat = DXGI_FORMAT_R24G8_TYPELESS;
	depth.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth.srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	depth.debugName = L"MeshPickingDepth";
	desc.depth = depth;

	renderTarget_.Create(graphicsCore.GetDXObject().GetDevice(),
		&graphicsCore.GetRTVDescriptor(), &graphicsCore.GetDSVDescriptor(),
		&graphicsCore.GetSRVDescriptor(), desc);
	readbackBuffer_.CreateBuffer(graphicsCore.GetDXObject().GetDevice());

	pendingReadback_ = false;
	initialized_ = true;
}

void Engine::MeshSubMeshPicker::Finalize() {

	renderTarget_.Destroy();
	pendingReadback_ = false;
	pendingFrameIndex_ = 0;
	initialized_ = false;
}

Engine::MeshSubMeshPickOutcome
Engine::MeshSubMeshPicker::ConsumePendingResult(
	GraphicsCore& graphicsCore, ECSWorld* world) {

	MeshSubMeshPickOutcome outcome{};
	if (!pendingReadback_) {
		return outcome;
	}

	const DxCommand* dxCommand =
		graphicsCore.GetDXObject().GetDxCommand();
	const uint64_t fenceValue =
		dxCommand->GetFrameFenceValue(pendingFrameIndex_);
	if (graphicsCore.GetDXObject().GetCommandQueue()->
		GetCompletedFenceValue() < fenceValue) {
		return outcome;
	}

	pendingReadback_ = false;
	outcome.committed = true;

	if (!world) {
		return outcome;
	}

	const PickResult& result = readbackBuffer_.GetReadbackData().result;
	if (result.valid == 0 ||
		result.entityIndex == UINT32_MAX ||
		result.entityGeneration == UINT32_MAX) {
		return outcome;
	}

	const Entity entity{ result.entityIndex, result.entityGeneration };
	if (!world->IsAlive(entity)) {
		return outcome;
	}

	outcome.hit = true;
	outcome.entity = entity;
	outcome.subMeshIndex = result.subMeshIndex;

	const std::span<const SubMeshMaterial> subMeshes =
		GetMeshSubMeshes(*world, entity);
	if (result.subMeshIndex < subMeshes.size()) {
		outcome.subMeshStableID = subMeshes[result.subMeshIndex].stableID;
	}
	return outcome;
}

void Engine::MeshSubMeshPicker::ExecuteReadback(GraphicsCore& graphicsCore) {

	if (!initialized_ || !renderTarget_.IsValid()) {
		return;
	}

	RenderTexture2D* sourceTexture = renderTarget_.GetColorTexture(0);
	if (!sourceTexture || !sourceTexture->GetResource()) {
		return;
	}

	DxCommand* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList = dxCommand->GetCommandList();
	sourceTexture->Transition(*dxCommand, D3D12_RESOURCE_STATE_COPY_SOURCE);

	D3D12_TEXTURE_COPY_LOCATION destination{};
	destination.pResource = readbackBuffer_.GetResource();
	destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	destination.PlacedFootprint.Footprint.Format =
		DXGI_FORMAT_R32G32B32A32_UINT;
	destination.PlacedFootprint.Footprint.Width = 1;
	destination.PlacedFootprint.Footprint.Height = 1;
	destination.PlacedFootprint.Footprint.Depth = 1;
	destination.PlacedFootprint.Footprint.RowPitch =
		D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

	D3D12_TEXTURE_COPY_LOCATION source{};
	source.pResource = sourceTexture->GetResource();
	source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	source.SubresourceIndex = 0;

	commandList->CopyTextureRegion(
		&destination, 0, 0, 0, &source, nullptr);
	pendingFrameIndex_ = dxCommand->GetCurrentFrameIndex();
	pendingReadback_ = true;
}
