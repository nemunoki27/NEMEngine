#include "MeshBatchResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/BackendDrawCommon.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/InvertedHullOutlineComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>

// c++
#include <unordered_map>

//============================================================================
//	MeshBatchResources classMethods
//============================================================================
namespace {

	bool CanCullView(const Engine::RenderDrawContext& drawContext, const Engine::MeshGPUResource& gpuMesh) {

		// スキニングメッシュはCPU側での静的Boundsがずれやすいため、ここでは安全側で除外する
		return drawContext.runtimeFeatures.useFrustumCulling &&
			drawContext.view && drawContext.cullingView && drawContext.cullingView->valid &&
			!gpuMesh.isSkinned;
	}

	const Engine::MeshRendererComponent* ResolveRenderer(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::MeshRendererComponent>(item->entity);
	}
	const Engine::SkinnedAnimationComponent* ResolveSkinnedAnimation(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::SkinnedAnimationComponent>(item->entity);
	}
	const Engine::InvertedHullOutlineComponent* ResolveOutline(const Engine::RenderItem* item) {

		if (!item || !item->world) {
			return nullptr;
		}
		return item->world->TryGetComponent<Engine::InvertedHullOutlineComponent>(item->entity);
	}

	// Hull本体を描くパスかどうかでOutlineStencilWriteは元メッシュ形状なのでHullではない
	bool IsHullOutlinePass(Engine::MaterialPassKind passKind) {

		return passKind == Engine::MaterialPassKind::Outline ||
			passKind == Engine::MaterialPassKind::OutlineStencilTest;
	}
}

Engine::MeshBatchResources::~MeshBatchResources() {

	Finalize();
}

void Engine::MeshBatchResources::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor* srvDescriptor = &graphicsCore.GetSRVDescriptor();
	// 可変stride構造化バッファを後から生成するため保持しておく
	device_ = device;
	srvDescriptor_ = srvDescriptor;

	// バッファ作成
	for (auto& viewBuffer : view_) {
		viewBuffer.Init(device);
	}
	meshData_.Init(device, srvDescriptor);
	// カリング後に残すインスタンスを書き込むRWバッファ
	visibleMeshData_.Init(device, srvDescriptor);
	draw_.Init(device);
	// ExecuteIndirect引数生成Computeに渡す固定Index数
	indirectArgs_.Init(device);
	screenSpaceOutlineMask_.Init(device);
	subMeshData_.Init(device, srvDescriptor);
	// 背面法アウトラインのインスタンス別GPUデータ
	outlineData_.Init(device, srvDescriptor);
	DxUtils::CreateUavBufferResource(device, indexedIndirectArgs_, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));
	indexedIndirectArgsState_ = D3D12_RESOURCE_STATE_COMMON;

	// 初期値を大きめにして、カメラ移動時の細かい再確保を減らす
	meshData_.EnsureCapacity(256);
	visibleMeshData_.EnsureCapacity(256);
	subMeshData_.EnsureCapacity(256);
	// GetOutlineGPUAddressが常に有効なリソースを指すよう、初期容量を確保しておく
	outlineData_.EnsureCapacity(256);
	meshScratch_.reserve(256);
	subMeshScratch_.reserve(256);

	// 初期化完了
	initialized_ = true;
}

void Engine::MeshBatchResources::Finalize() {

	// OptionalSkinningResourcesは内部にSRV/UAV付きGPUバッファを持つため、終了時に明示resetする
	skinning_.reset();
	// 可変strideマテリアルパラメータバッファのSRVとリソースを解放する
	if (srvDescriptor_ && subMeshParamSrvIndex_ != UINT32_MAX) {
		srvDescriptor_->Free(subMeshParamSrvIndex_);
		subMeshParamSrvIndex_ = UINT32_MAX;
	}
	subMeshParamBuffer_.Reset();
	subMeshParamMapped_ = nullptr;
	subMeshParamCapacityBytes_ = 0;
	subMeshParamStride_ = 0;
	subMeshParamElementCount_ = 0;
	subMeshParamAvailable_ = false;
	subMeshParamScratch_.clear();
	device_ = nullptr;
	srvDescriptor_ = nullptr;
	meshScratch_.clear();
	subMeshScratch_.clear();
	outlineScratch_.clear();
	paletteScratch_.clear();
	skinnedRecords_.clear();
	skinnedVertexOffsetMap_.clear();
	instanceCount_ = 0;
	skinnedInstanceCount_ = 0;
	skinningDispatched_ = false;
	usesFallbackTexture_ = false;
	indexedIndirectArgs_.Reset();
	indexedIndirectArgsState_ = D3D12_RESOURCE_STATE_COMMON;
	visibleMeshDataState_ = D3D12_RESOURCE_STATE_COMMON;
	initialized_ = false;
}

void Engine::MeshBatchResources::UpdateDrawConstants(const RenderDrawContext& drawContext,
	const MeshGPUResource& gpuMesh) {

	const bool hullOutline = IsHullOutlinePass(drawContext.passKind);
	bool cullingEnabled = CanCullView(drawContext, gpuMesh);
	if (cullingEnabled) {
		// カリング用カメラが取れない場合は全描画に倒す
		const ResolvedCameraView* cullingCamera = drawContext.cullingView->FindCamera(RenderCameraDomain::Perspective);
		if (!cullingCamera) {
			cullingEnabled = false;
		}
	}

	// ScreenPixelsでは近距離、投影、カメラ角度の影響を受ける
	// 誤カリングを避けるためHullのときだけ安全側でフラスタムカリングを無効にする
	if (hullOutline && outlineMetrics_.hasScreenPixelWidth) {
		cullingEnabled = false;
	}
	MeshDrawConstants drawConstants{};
	drawConstants.meshletCount = gpuMesh.meshletCount;
	drawConstants.subMeshCount = static_cast<uint32_t>(gpuMesh.subMeshes.size());
	drawConstants.instanceCount = instanceCount_;
	drawConstants.cullingEnabled = cullingEnabled ? 1u : 0u;
	drawConstants.packedMeshletVertexIndices = gpuMesh.usePackedMeshletVertexIndices ? 1u : 0u;

	// 背面法では通常メッシュのnormal cone判定を流用できない
	// 線が小さくても見えるためcontribution cullingも無効化する
	drawConstants.contributionCullingEnabled =
		(!hullOutline && cullingEnabled && drawContext.runtimeFeatures.useContributionCulling) ? 1u : 0u;
	drawConstants.normalConeCullingEnabled =
		(!hullOutline && cullingEnabled && drawContext.runtimeFeatures.useNormalConeCulling) ? 1u : 0u;

	drawConstants.meshBoundsCenter = gpuMesh.boundsCenter;
	drawConstants.meshBoundsRadius = gpuMesh.boundsRadius;
	// 小さすぎる値はチラつきや誤カリングの原因になるため、控えめな閾値にしている
	drawConstants.contributionPixelThreshold = 0.5f;

	drawConstants.invertedHullOutlinePass = hullOutline ? 1u : 0u;
	drawConstants.outlineMaxModelExpansion = hullOutline ? outlineMetrics_.maxModelExpansion : 0.0f;
	drawConstants.outlineMaxAbsCameraZOffset = hullOutline ? outlineMetrics_.maxAbsCameraZOffset : 0.0f;
	drawConstants.outlineHasScreenPixelWidth = (hullOutline && outlineMetrics_.hasScreenPixelWidth) ? 1u : 0u;

	draw_.Upload(drawConstants);

	if (drawContext.passKind == MaterialPassKind::ScreenSpaceOutlineMask ||
		drawContext.passKind == MaterialPassKind::ScreenSpaceOutlineCoverageMask) {

		ScreenSpaceOutlineMaskConstants params{};
		params.styleID = drawContext.screenSpaceOutlineMaskStyleID;
		params.restrictSubMeshIndex = drawContext.screenSpaceOutlineMaskRestrictSubMeshIndex;
		screenSpaceOutlineMask_.Upload(params);
	}
}

void Engine::MeshBatchResources::UpdateIndexedIndirectArgsConstants(uint32_t indexCount) {

	// ComputeでDrawIndexedInstanced引数を組み立てるため、Index数だけCPUから渡す
	MeshIndirectArgsConstants constants{};
	constants.indexCount = indexCount;
	indirectArgs_.Upload(constants);
}

void Engine::MeshBatchResources::EnsureSkinningResources(GraphicsCore& graphicsCore) {

	// すでにスキニング用のリソースがある場合は何もしない
	if (skinning_) {
		return;
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	SRVDescriptor* srvDescriptor = &graphicsCore.GetSRVDescriptor();

	// スキニング用のリソースを作成する
	skinning_ = std::make_unique<OptionalSkinningResources>();
	skinning_->skinningPalette.Init(device, srvDescriptor);
	skinning_->skinnedVertices.Init(device, srvDescriptor);
	// MeshShader経路のため、スキニング結果も圧縮頂点として保持する
	skinning_->skinnedPackedVertices.Init(device, srvDescriptor);
	skinning_->skinningConstants.Init(device);

	skinning_->skinningPalette.EnsureCapacity(256);
	skinning_->skinnedVertices.EnsureCapacity(256);
	skinning_->skinnedPackedVertices.EnsureCapacity(256);
	paletteScratch_.reserve(256);

	skinning_->skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
	skinning_->skinnedPackedVertexState = D3D12_RESOURCE_STATE_COMMON;
}

bool Engine::MeshBatchResources::FindSkinnedVertexOffset(ECSWorld* world, Entity entity, uint32_t& outVertexOffset) const {

	SkinnedEntityLookupKey key{};
	key.world = world;
	key.entity = entity;
	auto it = skinnedVertexOffsetMap_.find(key);
	if (it == skinnedVertexOffsetMap_.end()) {
		return false;
	}
	outVertexOffset = it->second;
	return true;
}

void Engine::MeshBatchResources::UpdateView(const ResolvedRenderView& view, const ResolvedRenderView* cullingView) {

	// 定数バッファにビュー行列を転送する
	MeshViewConstants constants{};
	if (const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective)) {

		constants.viewProjection = camera->matrices.viewProjectionMatrix;
		constants.renderCameraPos = camera->cameraPos;
	}
	constants.viewSize = Vector2(static_cast<float>((std::max)(view.width, 1u)),
		static_cast<float>((std::max)(view.height, 1u)));
	const ResolvedRenderView* cullView = cullingView ? cullingView : &view;
	if (const ResolvedCameraView* camera = cullView->FindCamera(RenderCameraDomain::Perspective)) {

		// SceneViewでは描画行列とカリング行列が別になるため、両方をGPUへ渡す
		constants.cullingViewProjection = camera->matrices.viewProjectionMatrix;
		constants.cullingView = camera->matrices.viewMatrix;
		constants.cullingCameraPos = camera->cameraPos;
		constants.cullingNearClip = camera->nearClip;
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
	view_[ToViewIndex(view.kind)].Upload(constants);
}

void Engine::MeshBatchResources::UploadBatchData(const RenderDrawContext& drawContext,
	const RenderSceneBatch& batch, const std::span<const RenderItem* const>& items, const MeshGPUResource& gpuMesh) {

	// staticキャッシュMISS時やSkinned/Billboardで毎フレーム走るバッチ構築のCPUコストを計測する
	FrameProfiler::ScopedSample profileSample(FrameProfiler::Category::MeshBatchUpload);

	// データクリア
	meshScratch_.clear();
	subMeshScratch_.clear();
	subMeshParamScratch_.clear();
	outlineScratch_.clear();
	paletteScratch_.clear();
	skinnedRecords_.clear();
	skinnedVertexOffsetMap_.clear();
	skinnedInstanceCount_ = 0;
	skinningDispatched_ = false;
	usesFallbackTexture_ = false;
	// アウトラインの保守的メトリクスを初期化する
	outlineMetrics_ = OutlineBatchMetrics{};
	// インスタンスと同数のアウトラインデータを必ず作るため、先に容量を確保する
	outlineScratch_.reserve(items.size());
	outlineData_.EnsureCapacity(static_cast<uint32_t>((std::max)(items.size(), size_t(1))));
	// 描画アイテム数に応じて必要なバッファサイズを確保する
	if (meshScratch_.capacity() < items.size()) {
		meshScratch_.reserve(items.size());
	}

	// サブメッシュデータはインスタンスごとに必要なため、アイテム数×サブメッシュ数の容量を確保する
	size_t totalSubMeshCount = items.size() * gpuMesh.subMeshes.size();
	if (subMeshScratch_.capacity() < totalSubMeshCount) {

		subMeshScratch_.reserve(totalSubMeshCount);
	}
	// カメラ移動で可視数が増えた瞬間にGPUバッファを作り直さないよう、カリング前の最大数で先に確保する
	meshData_.EnsureCapacity(static_cast<uint32_t>((std::max)(items.size(), size_t(1))));
	subMeshData_.EnsureCapacity(static_cast<uint32_t>((std::max)(totalSubMeshCount, size_t(1))));

	GraphicsCore& graphicsCore = *drawContext.graphicsCore;

	// スキニング可能メッシュのときだけリソース生成する
	if (gpuMesh.isSkinned) {

		EnsureSkinningResources(graphicsCore);
	}

	// エラーテクスチャのSRVインデックスを取得する
	const GPUTextureResource* fallback = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	uint32_t fallbackSRVIndex = (fallback && fallback->srvIndex != UINT32_MAX) ? fallback->srvIndex : 0;

	// テクスチャアセットIDからSRVインデックスを取得するヘルパー
	// assetIDが無効ならUINT32_MAXを返しシェーダー側で未使用として扱う
	auto ResolveSRVIndex = [&](AssetID assetID, bool sRGB) -> uint32_t {

		if (!assetID) {
			return UINT32_MAX;
		}
		const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(
			graphicsCore, drawContext.assetDatabase, assetID, sRGB);
		if (!texture || texture->srvIndex == UINT32_MAX) {
			return fallbackSRVIndex;
		}
		if (texture == fallback) {
			usesFallbackTexture_ = true;
		}
		return texture->srvIndex;
		};

	for (const RenderItem* item : items) {

		const MeshRenderPayload* payload = batch.GetPayload<MeshRenderPayload>(*item);
		if (!payload) {
			continue;
		}

		const MeshRendererComponent* renderer = ResolveRenderer(item);
		const SkinnedAnimationComponent* skinnedAnim = ResolveSkinnedAnimation(item);

		// MS/VS
		{
			const ResolvedRenderView* billboardView = drawContext.billboardView ? drawContext.billboardView : drawContext.view;
			MeshInstanceData instance{};
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(*item, *billboardView);
			MeshNormalMatrixResult instanceNormal = BuildSafeMeshNormalMatrix(instance.worldMatrix);
			instance.normalMatrix = instanceNormal.matrix;
			instance.orientationSign = instanceNormal.orientationSign;
			// 色はサブメッシュ単位のreflection paramへ移したのでper-instance tintは白固定にする
			instance.color = Color4::White();
			instance.subMeshDataOffset = static_cast<uint32_t>(subMeshScratch_.size());
			instance.subMeshCount = static_cast<uint32_t>(gpuMesh.subMeshes.size());

			// MeshRenderFlagsのうちピクセル側で参照するものをinstance.flagsへ写す
			const MeshRenderFlags renderFlags = renderer ? renderer->renderFlags : MeshRenderFlags::Default;
			if (HasMeshRenderFlag(renderFlags, MeshRenderFlags::Lighting)) {
				instance.flags |= kMeshInstanceFlagLighting;
			}
			if (HasMeshRenderFlag(renderFlags, MeshRenderFlags::ReceiveShadow)) {
				instance.flags |= kMeshInstanceFlagReceiveShadow;
			}
			if (HasMeshRenderFlag(renderFlags, MeshRenderFlags::ReceiveIBL)) {
				instance.flags |= kMeshInstanceFlagReceiveIBL;
			}
			if (HasMeshRenderFlag(renderFlags, MeshRenderFlags::ReceiveReflection)) {
				instance.flags |= kMeshInstanceFlagReceiveReflection;
			}

			// スキニングする場合の設定
			if (gpuMesh.isSkinned && skinning_ && skinnedAnim && skinnedAnim->runtimeInitialized &&
				skinnedAnim->palette.size() == gpuMesh.boneCount) {

				instance.flags |= kMeshInstanceFlagSkinned;
				instance.skinnedVertexOffset = skinnedInstanceCount_ * gpuMesh.vertexCount;

				// スキニングパレットデータを追加
				paletteScratch_.insert(paletteScratch_.end(), skinnedAnim->palette.begin(), skinnedAnim->palette.end());

				// スキニングするインスタンスのレコードを追加
				skinnedRecords_.push_back({ item->world,item->entity,instance.skinnedVertexOffset });
				SkinnedEntityLookupKey key{};
				key.world = item->world;
				key.entity = item->entity;
				skinnedVertexOffsetMap_[key] = instance.skinnedVertexOffset;

				// スキニングインスタンス数を加算
				++skinnedInstanceCount_;
			}

			// アウトラインGPUデータをインスタンスごとに必ず1件作る
			MeshOutlineGPUData outlineGPU{};
			if (const InvertedHullOutlineComponent* outline = ResolveOutline(item)) {

				outlineGPU.color = outline->color;
				outlineGPU.width = (std::max)(0.0f, outline->width);
				outlineGPU.cameraZOffset = outline->cameraZOffset;
				outlineGPU.expansionMode = static_cast<uint32_t>(outline->expansionMode);
				outlineGPU.widthMode = static_cast<uint32_t>(outline->widthMode);

				// Baked Normal / Outline SamplerはLinearとして解決する
				if (outline->useBakedNormal && outline->bakedNormalTexture) {
					outlineGPU.flags |= kMeshOutlineFlagUseBakedNormal;
					outlineGPU.bakedNormalTextureIndex = ResolveSRVIndex(outline->bakedNormalTexture, false);
				}
				if (outline->useOutlineSampler && outline->outlineSamplerTexture) {
					outlineGPU.flags |= kMeshOutlineFlagUseOutlineSampler;
					outlineGPU.outlineSamplerTextureIndex = ResolveSRVIndex(outline->outlineSamplerTexture, false);
				}

				// AS/instance-culling CS用の安全側メトリクスを更新する
				if (outlineGPU.widthMode == static_cast<uint32_t>(OutlineWidthMode::ScreenPixels)) {
					outlineMetrics_.hasScreenPixelWidth = true;
				} else {
					outlineMetrics_.maxModelExpansion = (std::max)(outlineMetrics_.maxModelExpansion, outlineGPU.width);
				}
				outlineMetrics_.maxAbsCameraZOffset = (std::max)(outlineMetrics_.maxAbsCameraZOffset, std::abs(outlineGPU.cameraZOffset));
			}

			instance.outlineDataIndex = static_cast<uint32_t>(outlineScratch_.size());
			outlineScratch_.emplace_back(outlineGPU);

			meshScratch_.emplace_back(instance);
		}

		for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(gpuMesh.subMeshes.size()); ++subMeshIndex) {

			// 色やテクスチャはreflection paramへ移したのでgSubMeshesには幾何情報のみ詰める
			MeshSubMeshShaderData data{};
			data.importedBaseColor = gpuMesh.subMeshes[subMeshIndex].baseColor;
			if (renderer && subMeshIndex < renderer->subMeshes.size()) {

				const auto& authoring = renderer->subMeshes[subMeshIndex];
				data.uvMatrix = authoring.uvMatrix;
				data.localMatrix = MeshSubMeshRuntime::BuildRenderLocalMatrix(authoring);
				// localMatrixからも法線変換行列を構築し最終的にinstance.normalMatrixと合成される
				const MeshNormalMatrixResult localNormal = BuildSafeMeshNormalMatrix(data.localMatrix);
				data.localNormalMatrix = localNormal.matrix;
				data.localOrientationSign = localNormal.orientationSign;
				// Position Scaling膨張の基準で原点基準にならないようサブメッシュのピボットを渡す
				data.sourcePivot = authoring.sourcePivot;
				// reflection paramの上書きをインスタンス×サブメッシュ単位で集める
				subMeshParamScratch_.emplace_back(authoring.parameterOverrides);
			} else {

				// rendererが無いときも要素数をgSubMeshesと揃える
				subMeshParamScratch_.emplace_back();
			}
			subMeshScratch_.emplace_back(data);
		}
	}

	// インスタンス数を設定
	instanceCount_ = static_cast<uint32_t>(meshScratch_.size());

	// GPUにデータ転送
	meshData_.Upload(meshScratch_);
	const uint32_t prevVisibleCapacity = visibleMeshData_.GetCapacity();
	// 可視インスタンスRWバッファはカリング前のインスタンス数分だけ確保する
	visibleMeshData_.EnsureCapacity(static_cast<uint32_t>((std::max)(meshScratch_.size(), size_t(1))));
	if (prevVisibleCapacity != visibleMeshData_.GetCapacity()) {

		visibleMeshDataState_ = D3D12_RESOURCE_STATE_COMMON;
	}
	subMeshData_.Upload(subMeshScratch_);
	// アウトラインGPUデータの転送でMeshDrawConstantsはUpdateDrawConstantsで毎描画更新する
	outlineData_.Upload(outlineScratch_);

	if (skinning_) {

		// パレットデータの転送
		skinning_->skinningPalette.Upload(paletteScratch_);

		// スキニング定数の転送
		MeshSkinningDispatchConstants skinningConstants{};
		skinningConstants.vertexCount = gpuMesh.vertexCount;
		skinningConstants.boneCount = gpuMesh.boneCount;
		skinningConstants.skinnedInstanceCount = skinnedInstanceCount_;
		skinning_->skinningConstants.Upload(skinningConstants);

		// スキニング頂点バッファの容量を確保
		const uint32_t requiredSkinnedVertexCount = (std::max)(1u,
			static_cast<uint32_t>((std::max)(items.size(), size_t(1))) * gpuMesh.vertexCount);
		const uint32_t prevCapacity = skinning_->skinnedVertices.GetCapacity();
		skinning_->skinnedVertices.EnsureCapacity(requiredSkinnedVertexCount);
		const uint32_t prevPackedCapacity = skinning_->skinnedPackedVertices.GetCapacity();
		// 通常頂点と圧縮頂点で別リソースなので、容量変更も個別に見る
		skinning_->skinnedPackedVertices.EnsureCapacity(requiredSkinnedVertexCount);
		// スキニング頂点バッファの容量が変わった場合は、リソース状態をリセットする
		if (prevCapacity != skinning_->skinnedVertices.GetCapacity()) {

			skinning_->skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;
		}
		if (prevPackedCapacity != skinning_->skinnedPackedVertices.GetCapacity()) {

			skinning_->skinnedPackedVertexState = D3D12_RESOURCE_STATE_COMMON;
		}
	}
}

void Engine::MeshBatchResources::UploadSubMeshMaterialParams(const MaterialAsset* material,
	const MaterialParameterLayout& layout, const RenderDrawContext& drawContext) {

	// シェーダーがMaterialParameters構造化バッファを宣言していないバッチはここで早期に無効化する
	subMeshParamAvailable_ = false;
	if (!layout.IsValid() || subMeshParamScratch_.empty() || !device_ || !srvDescriptor_) {
		return;
	}

	GraphicsCore& graphicsCore = *drawContext.graphicsCore;
	const GPUTextureResource* fallback = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	const uint32_t fallbackIndex = (fallback && fallback->srvIndex != UINT32_MAX) ? fallback->srvIndex : 0;

	// テクスチャparamのAssetIDをbindless indexへ解決する、名前でsRGB可否を判定する
	// 未指定はkNoTextureを返しシェーダー側でテクスチャなしの分岐に乗せる
	auto resolveTexture = [&](const std::string& name, const AssetID& id) -> uint32_t {

		if (!id) {
			return UINT32_MAX;
		}
		const bool sRGB = name.find("baseColor") != std::string::npos ||
			name.find("BaseColor") != std::string::npos ||
			name.find("emissive") != std::string::npos ||
			name.find("Emissive") != std::string::npos;
		const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(
			graphicsCore, drawContext.assetDatabase, id, sRGB);
		if (!texture || texture->srvIndex == UINT32_MAX) {
			return fallbackIndex;
		}
		return texture->srvIndex;
		};

	const std::unordered_map<std::string, MaterialParameterValue> emptyMap{};
	const std::unordered_map<std::string, MaterialParameterValue>& defaults =
		material ? material->parameters : emptyMap;

	// strideは16整列したレイアウトサイズでHLSLの構造化バッファ要素サイズと一致させる
	const uint32_t stride = (std::max)(layout.GetSizeInBytes(), 16u);
	const uint32_t elementCount = static_cast<uint32_t>(subMeshParamScratch_.size());
	std::vector<uint8_t> packed(static_cast<size_t>(stride) * elementCount, 0);
	for (uint32_t i = 0; i < elementCount; ++i) {

		const std::vector<uint8_t> element = MaterialParameterBufferBuilder::BuildElement(
			defaults, subMeshParamScratch_[i], layout, resolveTexture);
		const size_t copyBytes = (std::min)(static_cast<size_t>(stride), element.size());
		std::memcpy(packed.data() + static_cast<size_t>(stride) * i, element.data(), copyBytes);
	}

	// 容量不足やstride変更時のみリソースとSRVを作り直す
	const uint32_t requiredBytes = static_cast<uint32_t>(packed.size());
	if (requiredBytes > subMeshParamCapacityBytes_ || stride != subMeshParamStride_ || !subMeshParamBuffer_) {

		if (subMeshParamSrvIndex_ != UINT32_MAX) {
			srvDescriptor_->Free(subMeshParamSrvIndex_);
			subMeshParamSrvIndex_ = UINT32_MAX;
		}
		subMeshParamMapped_ = nullptr;
		const uint32_t newCapacityBytes = (std::max)(requiredBytes, 4096u);
		DxUtils::CreateBufferResource(device_, subMeshParamBuffer_, newCapacityBytes);
		HRESULT hr = subMeshParamBuffer_->Map(0, nullptr, reinterpret_cast<void**>(&subMeshParamMapped_));
		Assert::Call(SUCCEEDED(hr), "failed to map subMesh material parameter buffer");

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = (std::max)(newCapacityBytes / stride, 1u);
		srvDesc.Buffer.StructureByteStride = stride;
		srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		srvDescriptor_->CreateSRV(subMeshParamSrvIndex_, subMeshParamBuffer_.Get(), srvDesc);
		subMeshParamHandle_ = srvDescriptor_->GetGPUHandle(subMeshParamSrvIndex_);
		subMeshParamCapacityBytes_ = newCapacityBytes;
		subMeshParamStride_ = stride;
	}

	if (subMeshParamMapped_ && !packed.empty()) {
		std::memcpy(subMeshParamMapped_, packed.data(), packed.size());
	}
	subMeshParamElementCount_ = elementCount;
	subMeshParamAvailable_ = true;
}