#include "MeshBatchViewResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>
#include <cmath>

namespace {
	bool CanCullView(const Engine::RenderDrawContext& drawContext, const Engine::MeshGPUResource& gpuMesh) {

		// スキニングメッシュはCPU側での静的Boundsがずれやすいため、ここでは安全側で除外する
		return drawContext.view &&
			drawContext.cullingView &&
			drawContext.cullingView->valid &&
			!gpuMesh.isSkinned;
	}

	bool IsHullOutlinePass(Engine::MaterialPassKind passKind) {

		return passKind == Engine::MaterialPassKind::Outline ||
			passKind == Engine::MaterialPassKind::OutlineStencilTest;
	}
}

void Engine::MeshBatchViewResources::BeginDynamicConstantsFrame() {

	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (dynamicConstantFrameSerial_ == frameSerial) {
		return;
	}
	dynamicConstantAllocator_.BeginFrame();
	dynamicConstantFrameSerial_ = frameSerial;
}

void Engine::MeshBatchViewResources::UpdateDrawConstants(const RenderDrawContext& drawContext,
	const MeshGPUResource& gpuMesh, uint32_t subMeshIndex,
	uint32_t subMeshGroupIndex, ID3D12Device* device, uint32_t instanceCount,
	const OutlineBatchMetrics& outlineMetrics, float maxDisplacement) {

	const bool hullOutline = IsHullOutlinePass(drawContext.passKind);
	bool canCull = CanCullView(drawContext, gpuMesh);
	if (canCull) {
		// カリング用カメラが取れない場合は全描画に倒す
		const ResolvedCameraView* cullingCamera = drawContext.cullingView->FindCamera(RenderCameraDomain::Perspective);
		if (!cullingCamera) {
			canCull = false;
		}
	}

	// ScreenPixelsでは近距離、投影、カメラ角度の影響を受ける
	// 誤カリングを避けるためHullのときだけ安全側でフラスタムカリングを無効にする
	if (hullOutline && outlineMetrics.hasScreenPixelWidth) {
		canCull = false;
	}
	const bool frustumCullingEnabled =
		canCull &&
		drawContext.runtimeFeatures.useFrustumCulling;
	const bool contributionCullingEnabled =
		!hullOutline && canCull &&
		drawContext.runtimeFeatures.useContributionCulling;
	const bool normalConeCullingEnabled =
		!hullOutline && canCull &&
		drawContext.runtimeFeatures.useNormalConeCulling;
	const bool occlusionCullingEnabled =
		!hullOutline && canCull &&
		drawContext.passKind != MaterialPassKind::ZPrepass &&
		drawContext.passKind != MaterialPassKind::EditorPicking &&
		drawContext.runtimeFeatures.useOcclusionCulling &&
		drawContext.occlusionDepthPyramidReady;
	const bool cullingEnabled =
		frustumCullingEnabled ||
		contributionCullingEnabled ||
		normalConeCullingEnabled ||
		occlusionCullingEnabled;
	MeshDrawConstants drawConstants{};
	drawConstants.meshletCount = 0;
	drawConstants.subMeshCount = static_cast<uint32_t>(gpuMesh.subMeshes.size());
	drawConstants.instanceCount = instanceCount;
	drawConstants.cullingEnabled = cullingEnabled ? 1u : 0u;
	drawConstants.packedMeshletVertexIndices = gpuMesh.usePackedMeshletVertexIndices ? 1u : 0u;
	drawConstants.frustumCullingEnabled =
		frustumCullingEnabled ? 1u : 0u;

	// 背面法では通常メッシュのnormal cone判定を流用できない
	// 線が小さくても見えるためcontribution cullingも無効化する
	drawConstants.contributionCullingEnabled =
		contributionCullingEnabled ? 1u : 0u;
	drawConstants.normalConeCullingEnabled =
		normalConeCullingEnabled ? 1u : 0u;
	drawConstants.occlusionCullingEnabled =
		occlusionCullingEnabled ? 1u : 0u;
	drawConstants.subMeshGroupIndex = subMeshGroupIndex;

	drawConstants.meshBoundsCenter = gpuMesh.boundsCenter;
	drawConstants.meshBoundsRadius = gpuMesh.boundsRadius + maxDisplacement;
	drawConstants.maxDisplacement = maxDisplacement;
	// 小さすぎる値はチラつきや誤カリングの原因になるため、控えめな閾値にしている
	drawConstants.contributionPixelThreshold = 0.5f;
	drawConstants.lodPixelThresholds = Vector3(
		drawContext.runtimeFeatures.
			meshLOD0PixelThreshold,
		drawContext.runtimeFeatures.
			meshLOD1PixelThreshold,
		drawContext.runtimeFeatures.
			meshLOD2PixelThreshold);
	drawConstants.lodCount =
		drawContext.runtimeFeatures.useMeshLOD ?
		kMeshLODCount : 1u;

	drawConstants.invertedHullOutlinePass = hullOutline ? 1u : 0u;
	drawConstants.outlineMaxModelExpansion = hullOutline ? outlineMetrics.maxModelExpansion : 0.0f;
	drawConstants.outlineMaxAbsCameraZOffset = hullOutline ? outlineMetrics.maxAbsCameraZOffset : 0.0f;
	drawConstants.outlineHasScreenPixelWidth = (hullOutline && outlineMetrics.hasScreenPixelWidth) ? 1u : 0u;
	const bool drawSingleSubMesh =
		subMeshIndex != kAllMeshSubMeshes &&
		subMeshIndex < gpuMesh.subMeshes.size();
	for (uint32_t lodIndex = 0; lodIndex < kMeshLODCount; ++lodIndex) {

		const MeshLODRange& lod = drawSingleSubMesh ?
			gpuMesh.subMeshes[subMeshIndex].lods[lodIndex] :
			gpuMesh.lods[lodIndex];
		drawConstants.lodIndexOffsets[lodIndex] = lod.indexOffset;
		drawConstants.lodIndexCounts[lodIndex] = lod.indexCount;
		drawConstants.lodMeshletOffsets[lodIndex] = lod.meshletOffset;
		drawConstants.lodMeshletCounts[lodIndex] = lod.meshletCount;
		drawConstants.meshletCount = (std::max)(
			drawConstants.meshletCount, lod.meshletCount);
	}

	BeginDynamicConstantsFrame();
	drawGPUAddress_ =
		dynamicConstantAllocator_.AllocateAndUpload(
			*retirement_,
			device, drawConstants).gpuAddress;

	if (drawContext.passKind == MaterialPassKind::ScreenSpaceOutlineMask ||
		drawContext.passKind == MaterialPassKind::ScreenSpaceOutlineCoverageMask) {

		ScreenSpaceOutlineMaskConstants params{};
		params.styleID = drawContext.screenSpaceOutlineMaskStyleID;
		params.restrictSubMeshIndex = drawContext.screenSpaceOutlineMaskRestrictSubMeshIndex;
		params.alphaSource = drawContext.screenSpaceOutlineMaskAlphaSource;
		screenSpaceOutlineMaskGPUAddress_ =
			dynamicConstantAllocator_.AllocateAndUpload(*retirement_, device, params).gpuAddress;
	}
}

void Engine::MeshBatchViewResources::UpdateIndexedIndirectArgsConstants(uint32_t indexCount, ID3D12Device* device) {

	// ComputeでDrawIndexedInstanced引数を組み立てるため、Index数だけCPUから渡す
	MeshIndirectArgsConstants constants{};
	constants.indexCount = indexCount;
	BeginDynamicConstantsFrame();
	indirectArgsGPUAddress_ =
		dynamicConstantAllocator_.AllocateAndUpload(*retirement_, device, constants).gpuAddress;
}

void Engine::MeshBatchViewResources::UpdateView(const ResolvedRenderView& view, const ResolvedRenderView* cullingView) {

	const size_t viewIndex = ToViewIndex(view.kind);
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (viewUploadFrameSerials_[viewIndex] == frameSerial) {
		return;
	}
	viewUploadFrameSerials_[viewIndex] = frameSerial;

	// 定数バッファにビュー行列を転送する
	MeshViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective)) {

		constants.viewProjection = camera->matrices.viewProjectionMatrix;
		constants.previousViewProjection = previousViewValid_[viewIndex] ?
			previousViewProjections_[viewIndex] : constants.viewProjection;
		previousViewProjections_[viewIndex] = constants.viewProjection;
		previousViewValid_[viewIndex] = true;
		constants.renderCameraPos = camera->cameraPos;
	}
	constants.frameSerial = static_cast<uint32_t>(frameSerial);
	constants.viewSize = Vector2(static_cast<float>((std::max)(view.width, 1u)),
		static_cast<float>((std::max)(view.height, 1u)));
	const ResolvedRenderView* cullView = cullingView ? cullingView : &view;
	if (const ResolvedCameraView* camera = cullView->FindCamera(RenderCameraDomain::Perspective)) {

		// SceneViewでは描画行列とカリング行列が別になるため、両方をGPUへ渡す
		constants.cullingViewProjection = camera->matrices.viewProjectionMatrix;
		constants.cullingView = camera->matrices.viewMatrix;
		constants.cullingCameraPos = camera->cameraPos;
		constants.cullingNearClip = camera->nearClip;
		constants.cullingCameraForward = camera->forward;
		constants.cullingViewSize = Vector2(static_cast<float>((std::max)(cullView->width, 1u)),
			static_cast<float>((std::max)(cullView->height, 1u)));
		constants.cullingProjectionScale = Vector2(
			std::abs(camera->matrices.projectionMatrix.m[0][0]),
			std::abs(camera->matrices.projectionMatrix.m[1][1]));
	} else {
		constants.cullingViewProjection = constants.viewProjection;
		constants.cullingView = Matrix4x4::Identity();
		constants.cullingViewSize = constants.viewSize;
		constants.cullingProjectionScale = Vector2::AnyInit(1.0f);
	}
	view_[viewIndex].Upload(constants);
}

void Engine::MeshBatchViewResources::Init(GraphicsResourceRetirement& retirement, ID3D12Device* device) {

	if (retirement_ && retirement_ != &retirement) throw std::logic_error("Mesh定数Bufferの回収先は変更できません");
	retirement_ = &retirement;

	for (auto& viewBuffer : view_) {
		viewBuffer.Init(retirement, device);
	}
}

void Engine::MeshBatchViewResources::Release() {

	dynamicConstantAllocator_.Release();
	dynamicConstantFrameSerial_ = 0;
	viewUploadFrameSerials_ = { 0, 0 };
	drawGPUAddress_ = 0;
	screenSpaceOutlineMaskGPUAddress_ = 0;
	indirectArgsGPUAddress_ = 0;
}
