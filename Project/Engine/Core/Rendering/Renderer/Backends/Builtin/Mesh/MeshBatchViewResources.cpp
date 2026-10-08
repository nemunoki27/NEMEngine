#include "MeshBatchViewResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Outline/ScreenSpaceOutlineGPUTypes.h>

// c++
#include <cmath>
#include <cstring>

namespace {

	// 静的Boundsでカリングできるか
	bool CanCullView(const Engine::RenderDrawContext& drawContext, const Engine::MeshGPUResource& gpuMesh) {

		// Skinningは静的Boundsによる省略を避ける
		return drawContext.view &&
			drawContext.cullingView &&
			drawContext.cullingView->valid &&
			!gpuMesh.isSkinned;
	}

	// 背面Outlineの膨張描画か
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
	const OutlineBatchMetrics& outlineMetrics, float maxDisplacement, bool normalConeAllowed) {

	const bool hullOutline = IsHullOutlinePass(drawContext.passKind);
	bool canCull = CanCullView(drawContext, gpuMesh);
	if (canCull) {
		// カリング用カメラが取れない場合は全描画に倒す
		const ResolvedCameraView* cullingCamera = drawContext.cullingView->FindCamera(RenderCameraDomain::Perspective);
		if (!cullingCamera) {
			canCull = false;
		}
	}

	// 画面幅のOutlineはBoundsによる省略を避ける
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
		!hullOutline && canCull && normalConeAllowed && maxDisplacement == 0.0f &&
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

	// 輪郭の形状を省略せず描画
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
	// 小さい形状の省略によるちらつきを抑える
	drawConstants.contributionPixelThreshold = 0.5f;
	drawConstants.lodPixelThresholds = Vector3(
		drawContext.runtimeFeatures.
			meshLOD0PixelThreshold,
		drawContext.runtimeFeatures.
			meshLOD1PixelThreshold,
		drawContext.runtimeFeatures.
			meshLOD2PixelThreshold);
	drawConstants.lodCount =
		drawContext.runtimeFeatures.useMeshLOD &&
		drawContext.passKind != MaterialPassKind::Transparent ?
		kMeshLODCount : 1u;
	drawConstants.lodDitherEnabled =
		drawConstants.lodCount > 1u && gpuMesh.ditherLODTransitions &&
		!drawContext.disableLODDither ? 1u : 0u;
	drawConstants.preserveInstanceOrder =
		drawContext.passKind == MaterialPassKind::Transparent ? 1u : 0u;

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
		drawConstants.meshletCount = (std::max)(drawConstants.meshletCount, lod.meshletCount);
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
		params.alphaThreshold = drawContext.screenSpaceOutlineMaskAlphaThreshold;
		screenSpaceOutlineMaskGPUAddress_ =
			dynamicConstantAllocator_.AllocateAndUpload(*retirement_, device, params).gpuAddress;
	}
}

void Engine::MeshBatchViewResources::UpdateIndexedIndirectArgsConstants(uint32_t indexCount, ID3D12Device* device) {

	// 間接描画のIndex数を転送
	MeshIndirectArgsConstants constants{};
	constants.indexCount = indexCount;
	BeginDynamicConstantsFrame();
	indirectArgsGPUAddress_ =
		dynamicConstantAllocator_.AllocateAndUpload(*retirement_, device, constants).gpuAddress;
}

void Engine::MeshBatchViewResources::UpdateView(const ResolvedRenderView& view,
	const ResolvedRenderView* cullingView, const ResolvedRenderView* lodView) {

	const size_t viewIndex = ToViewIndex(view.kind);
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	const bool firstViewInFrame = viewUploadFrameSerials_[viewIndex] != frameSerial;

	// 定数バッファにビュー行列を転送する
	MeshViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective)) {

		constants.viewProjection = camera->matrices.viewProjectionMatrix;
		// 補助描画では画面Cameraの履歴を使う
		constants.previousViewProjection = cameraHistory_.Update(
			lodView ? *lodView : view, RenderCameraDomain::Perspective, frameSerial);
		constants.renderCameraPos = camera->cameraPos;
	}
	// Shadow Mapなどの補助描画は画面CameraのLODを維持する
	const ResolvedRenderView* resolvedLODView = lodView ? lodView : &view;
	if (const ResolvedCameraView* camera =
		resolvedLODView->FindCamera(RenderCameraDomain::Perspective)) {

		constants.lodView = camera->matrices.viewMatrix;
		constants.lodNearClip = camera->nearClip;
		constants.lodOrthographic = camera->projectionMode == ResolvedProjectionMode::Orthographic ? 1u : 0u;
		constants.lodProjectionScale = Vector2(
			std::abs(camera->matrices.projectionMatrix.m[0][0]),
			std::abs(camera->matrices.projectionMatrix.m[1][1]));
	}
	constants.frameSerial = static_cast<uint32_t>(frameSerial);
	constants.viewSize = Vector2(static_cast<float>((std::max)(view.width, 1u)),
		static_cast<float>((std::max)(view.height, 1u)));
	const ResolvedRenderView* cullView = cullingView ? cullingView : &view;
	if (const ResolvedCameraView* camera = cullView->FindCamera(RenderCameraDomain::Perspective)) {

		// 描画とカリングのCameraを分けて転送
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
	// 同じ種類のViewでもCameraが変われば別の定数領域へ転送する
	if (firstViewInFrame || std::memcmp(&uploadedViews_[viewIndex], &constants, sizeof(constants)) != 0) {
		view_[viewIndex].Upload(constants);
		uploadedViews_[viewIndex] = constants;
		viewUploadFrameSerials_[viewIndex] = frameSerial;
	}
}

void Engine::MeshBatchViewResources::Init(GraphicsResourceRetirement& retirement, ID3D12Device* device) {

	if (retirement_ && retirement_ != &retirement) throw std::logic_error("Mesh定数Bufferの回収先は変更できません");
	retirement_ = &retirement;

	for (auto& viewBuffer : view_) {
		viewBuffer.Init(retirement, device);
	}
}

void Engine::MeshBatchViewResources::Release() {

	// 再利用時へ前のWorldのViewと履歴を持ち越さない
	for (auto& view : view_) {
		view.Release();
	}
	cameraHistory_.Clear();
	retirement_ = nullptr;
	dynamicConstantAllocator_.Release();
	dynamicConstantFrameSerial_ = 0;
	viewUploadFrameSerials_ = { 0, 0 };
	drawGPUAddress_ = 0;
	screenSpaceOutlineMaskGPUAddress_ = 0;
	indirectArgsGPUAddress_ = 0;
}
