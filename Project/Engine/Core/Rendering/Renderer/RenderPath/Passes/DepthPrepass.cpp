#include "DepthPrepass.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPassExecutionHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthPyramidTexture.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

// c++
#include <algorithm>

namespace {

	struct DepthPyramidConstants {

		uint32_t sourceWidth = 0;
		uint32_t sourceHeight = 0;
		uint32_t destinationWidth = 0;
		uint32_t destinationHeight = 0;
		uint32_t copySource = 0;
		uint32_t _pad0[3] = { 0, 0, 0 };
	};
	static_assert(sizeof(DepthPyramidConstants) % 16 == 0);
}

//============================================================================
//	DepthPrepass classMethods
//============================================================================
Engine::DepthPrepass::DepthPrepass(
	const RenderPipelineDeps& deps) :
	deps_(deps) {

	constantsCBVSlot_ =
		bindCache_.AddSlot("DepthPyramidConstants",
			ShaderBindingKind::CBV);
	sourceDepthSRVSlot_ =
		bindCache_.AddSlot("gSourceDepth",
			ShaderBindingKind::SRV);
	outputDepthUAVSlot_ =
		bindCache_.AddSlot("gOutputDepth",
			ShaderBindingKind::UAV);
}

void Engine::DepthPrepass::Execute(GraphicsCore& graphicsCore,
	const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) {

	// 共有リソースが無ければ描画不可
	if (!context.resources) {
		return;
	}

	// 不透明アイテムから深度に描画する対象のみ取得
	std::vector<const RenderItem*> items = CollectItems(context, passBuckets);
	RenderPassExecutionHelper::Execute(graphicsCore, context, items, deps_,
		context.resources->GetSceneMain(), MaterialPassKind::ZPrepass, false, true);

	// カリング対象View自身の深度だけを生成し、SceneViewのGame基準時はGame側を再利用する
	if (context.cullingResources == context.resources &&
		graphicsCore.GetDXObject().GetFeatureController()
			.ShouldUseOcclusionCulling()) {
		BuildDepthPyramid(graphicsCore, context);
	}
}

std::vector<const Engine::RenderItem*> Engine::DepthPrepass::CollectItems(
	const SceneExecutionContext& context, const RenderPassPhaseBuckets& passBuckets) const {

	// 不透明アイテムが空ならZPrepassの対象も無い
	std::vector<const RenderItem*> result{};
	const RenderPassItemList& list = passBuckets.Get(RenderPhase::Opaque);
	if (list.IsEmpty()) {
		return result;
	}
	// 深度は透視投影カメラ基準で書くため対応するカメラが無ければ描画不可
	const ResolvedCameraView* camera = context.view->FindCamera(RenderCameraDomain::Perspective);
	if (!camera) {
		return result;
	}

	result.reserve(list.items.size());
	for (const RenderItem* item : list.items) {

		if (!item) {
			continue;
		}
		// メッシュ以外は深度描画なし
		if (item->backendID != RenderBackendID::Mesh) {
			continue;
		}
		// Maskedはベースカラーαを評価する専用深度パスが無いためGBuffer描画で深度を書き込む
		if (item->surfaceMode == MaterialSurfaceMode::Masked) {
			continue;
		}
		// カメラのカリングマスクで弾かれるレイヤーは除外する
		if ((item->visibilityLayerMask & camera->cullingMask) == 0) {
			continue;
		}
		// メッシュペイロードデータの中で深度描画が有効な場合のみ
		const MeshRenderPayload* payload = deps_.renderBatch->GetPayload<MeshRenderPayload>(*item);
		if (!payload || !payload->enableZPrepass) {
			continue;
		}
		result.emplace_back(item);
	}
	return result;
}

void Engine::DepthPrepass::BuildDepthPyramid(
	GraphicsCore& graphicsCore, SceneExecutionContext& context) {

	if (!context.resources || !deps_.pipelineCache ||
		!deps_.assetLibrary) {
		return;
	}

	MultiRenderTarget* sceneMain =
		context.resources->GetSceneMain();
	DepthTexture2D* sourceDepth = sceneMain ?
		sceneMain->GetDepthTexture() : nullptr;
	DepthPyramidTexture& pyramid =
		context.resources->GetDepthPyramid();
	if (!sourceDepth || !pyramid.IsValid()) {
		return;
	}

	if (!depthPyramidPipeline_) {
		depthPyramidPipeline_ =
			BuiltinAssets::Pipelines::BuildDepthPyramid;
	}
	const PipelineState* pipelineState =
		deps_.pipelineCache->GetORCreate(
			graphicsCore.GetDXObject(), *deps_.assetLibrary,
			depthPyramidPipeline_, PipelineVariantKind::Compute,
			{}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState ||
		!pipelineState->GetComputePipeline()) {
		return;
	}

	DxCommand* dxCommand =
		graphicsCore.GetDXObject().GetDxCommand();
	ID3D12GraphicsCommandList6* commandList =
		dxCommand->GetCommandList();
	dxCommand->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap()
		});
	commandList->SetComputeRootSignature(
		pipelineState->GetRootSignature());
	commandList->SetPipelineState(
		pipelineState->GetComputePipeline());
	bindCache_.Sync(*pipelineState);

	const uint64_t frameSerial =
		GraphicsFrameState::GetFrameSerial();
	if (constantAllocatorFrameSerial_ != frameSerial) {
		constantAllocator_.BeginFrame();
		constantAllocatorFrameSerial_ = frameSerial;
	}

	sourceDepth->Transition(
		*dxCommand,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	uint32_t sourceWidth = sourceDepth->GetWidth();
	uint32_t sourceHeight = sourceDepth->GetHeight();
	for (uint32_t mipIndex = 0;
		mipIndex < pyramid.GetMipCount(); ++mipIndex) {

		const uint32_t destinationWidth =
			(mipIndex == 0) ? sourceWidth :
			(std::max)(1u, sourceWidth / 2u);
		const uint32_t destinationHeight =
			(mipIndex == 0) ? sourceHeight :
			(std::max)(1u, sourceHeight / 2u);

		DepthPyramidConstants constants{};
		constants.sourceWidth = sourceWidth;
		constants.sourceHeight = sourceHeight;
		constants.destinationWidth = destinationWidth;
		constants.destinationHeight = destinationHeight;
		constants.copySource = (mipIndex == 0) ? 1u : 0u;
		const auto allocation =
			constantAllocator_.AllocateAndUpload(graphicsCore.GetDXObject().GetResourceRetirement(),
			graphicsCore.GetDXObject().GetDevice(),
				constants);

		pyramid.TransitionMip(
			*dxCommand, mipIndex,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		if (bindCache_.Has(constantsCBVSlot_)) {
			RootBindingCommand::SetComputeCBV(
				commandList,
				bindCache_.Get(constantsCBVSlot_),
				allocation.gpuAddress);
		}
		if (bindCache_.Has(sourceDepthSRVSlot_)) {
			const D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle =
				(mipIndex == 0) ?
				sourceDepth->GetSRVGPUHandle() :
				pyramid.GetMipSRVGPUHandle(mipIndex - 1);
			RootBindingCommand::SetComputeSRV(
				commandList,
				bindCache_.Get(sourceDepthSRVSlot_),
				0, sourceHandle);
		}
		if (bindCache_.Has(outputDepthUAVSlot_)) {
			RootBindingCommand::SetComputeUAV(
				commandList,
				bindCache_.Get(outputDepthUAVSlot_),
				0,
				pyramid.GetMipUAVGPUHandle(mipIndex));
		}

		commandList->Dispatch(
			DxUtils::RoundUp(destinationWidth, 8),
			DxUtils::RoundUp(destinationHeight, 8), 1);
		dxCommand->UAVBarrier(pyramid.GetResource());
		pyramid.TransitionMip(
			*dxCommand, mipIndex,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

		sourceWidth = destinationWidth;
		sourceHeight = destinationHeight;
	}
	pyramid.MarkBuilt(frameSerial);
	context.occlusionDepthPyramidReady = true;
}
