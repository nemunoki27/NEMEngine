#include "ParticleRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/World/Components/Rendering/ParticleEmitterComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BillboardComponent.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleRenderBackend internal
//============================================================================
namespace {

	// 描画で渡す定数バッファ
	struct ParticleViewConstants {

		Engine::Matrix4x4 viewProjection = Engine::Matrix4x4::Identity();
		Engine::Vector3 cameraPosition = Engine::Vector3::AnyInit(0.0f);
		float pad0 = 0.0f;
	};
	// パラメトリック形状生成で渡す定数バッファ
	struct ParticleShapeConstants {

		uint32_t divide = 16;
		uint32_t pad0 = 0;
		uint32_t pad1 = 0;
		uint32_t pad2 = 0;
	};

	// MeshShaderの1グループが担当する三角形数
	constexpr uint32_t kParticleMeshGroupTriangles = 64;

	// エフェクトのマテリアルを解決する、未設定は描画空間に応じたビルトインの既定マテリアルへ落とす
	bool ResolveParticlePass(const Engine::RenderDrawContext& context, Engine::AssetID requestedMaterial,
		bool is2D, Engine::BackendDrawCommon::ResolvedMaterialPass& outResolved) {

		const Engine::AssetID defaultMaterial = is2D ?
			Engine::BuiltinAssets::Materials::DefaultParticle2D : Engine::BuiltinAssets::Materials::DefaultParticle;
		Engine::AssetID materialID = requestedMaterial ? requestedMaterial : defaultMaterial;
		const Engine::MaterialAsset* material = context.assetLibrary->LoadMaterial(materialID);
		if (!material) {

			materialID = defaultMaterial;
			material = context.assetLibrary->LoadMaterial(materialID);
		}
		if (!material) {
			return false;
		}
		// 実行中のパス種別を優先し、無ければTransparentとDrawの順で落とす
		const Engine::MaterialPassBinding* pass = Engine::FindPass(*material, context.passKind);
		if (!pass) {
			pass = Engine::FindPass(*material, Engine::MaterialPassKind::Transparent);
		}
		if (!pass) {
			pass = Engine::FindPass(*material, Engine::MaterialPassKind::Draw);
		}
		if (!pass) {
			return false;
		}
		outResolved.materialID = materialID;
		outResolved.material = material;
		outResolved.pass = pass;
		return true;
	}

	// 描画設定から共有ジオメトリ生成用のコンポーネントを作る
	Engine::PrimitiveRendererComponent MakeShapeComponent(const Engine::ParticleRenderSettings& settings) {

		Engine::PrimitiveRendererComponent shape{};
		shape.type = settings.shape;
		shape.plane = settings.plane;
		shape.crossPlane = settings.crossPlane;
		shape.ring = settings.ring;
		shape.cylinder = settings.cylinder;
		shape.sphere = settings.sphere;
		shape.hemisphere = settings.hemisphere;
		shape.cube = settings.cube;
		return shape;
	}

	// 形状アニメのパラメトリックMS生成を使うか、MS対応GPUかつRing/Cylinderのみ
	bool UseParametricShape(const Engine::RenderDrawContext& context, const Engine::ParticleRenderSettings& settings) {

		if (!settings.shapeOverLifetime || settings.model ||
			settings.space == Engine::PrimitiveRenderSpace::Screen2D) {
			return false;
		}
		if (settings.shape != Engine::PrimitiveType::Ring && settings.shape != Engine::PrimitiveType::Cylinder) {
			return false;
		}
		return context.runtimeFeatures.useMeshShader && !context.forceVertexMeshVariant;
	}
}

//============================================================================
//	ParticleRenderBackend classMethods
//============================================================================
Engine::ParticleRenderBackend::~ParticleRenderBackend() {

	geometryManager_.Clear();
	meshResourceManager_.Finalize();
	resourcePool_.Clear();
}

void Engine::ParticleRenderBackend::BeginFrame(GraphicsCore& graphicsCore) {

	if (!geometryManagerInitialized_) {
		geometryManager_.Init(graphicsCore);
		geometryManagerInitialized_ = true;
	}
	if (!meshManagerInitialized_) {
		meshResourceManager_.Init(graphicsCore);
		meshManagerInitialized_ = true;
	}
	geometryManager_.BeginFrame();
	meshResourceManager_.BeginFrame(graphicsCore);
	resourcePool_.BeginFrame();
	BeginFrameCommon();
}

void Engine::ParticleRenderBackend::CollectInstances(const RenderDrawContext& context,
	std::span<const RenderItem* const> items, std::vector<ParticleInstanceData>& outInstances) const {

	outInstances.clear();
	for (const RenderItem* item : items) {

		const ParticleRenderPayload* payload = context.batch->GetPayload<ParticleRenderPayload>(*item);
		if (!payload || !payload->emitter) {
			continue;
		}
		const ParticleRenderSettings& settings = payload->emitter->runtimeRenderSettings;

		// ビルボードは描画中のビューのカメラへ向ける、BillboardComponentと同じ軸マスク方式
		const ResolvedCameraView* camera = context.view ? context.view->FindCamera(item->cameraDomain) : nullptr;
		const bool useBillboard = settings.space == PrimitiveRenderSpace::World3D &&
			!settings.billboardAxes.empty() && camera && camera->valid;
		BillboardComponent axisMask{};
		axisMask.axes = settings.billboardAxes;

		outInstances.reserve(outInstances.size() + payload->emitter->runtimeParticles.size());
		for (const Particle& particle : payload->emitter->runtimeParticles) {

			// 粒子はワールド空間でシミュレーション済み
			const Vector3 worldPos = particle.position;
			// 面内回転を掛けてからカメラへ向ける
			const Quaternion roll = Quaternion::MakeAxisAngle(
				Vector3(0.0f, 0.0f, 1.0f), particle.rotation * Math::radian);
			Quaternion rotation = roll;
			if (useBillboard) {

				const Quaternion desired = RenderBillboard::MakeCameraBillboardRotation(*camera, worldPos);
				if (settings.billboardAxes.size() == 3) {
					rotation = desired * roll;
				} else {

					// 一部軸のみのビルボードは軸マスクで合成する
					const Vector3 localForward = Vector3::NormalizeOr(Vector3::Transform(
						Vector3(0.0f, 0.0f, 1.0f), Quaternion::MakeRotateMatrix(desired)), Vector3(0.0f, 0.0f, 1.0f));
					rotation = RenderBillboard::ApplyAxisMask(roll, desired, axisMask, localForward);
				}
			}

			ParticleInstanceData instance{};
			instance.worldMatrix = Matrix4x4::MakeAffineMatrix(Vector3::AnyInit(particle.size), rotation, worldPos);
			instance.color = particle.color;
			instance.uvScaleOffset = Vector4(particle.uvScale.x, particle.uvScale.y,
				particle.uvOffset.x, particle.uvOffset.y);
			instance.shapeParams = particle.shapeParams;
			outInstances.emplace_back(instance);
		}

		// 半透明の重なりを正しく見せるため、奥から手前の順へ並べ替える
		if (settings.sortMode == ParticleSortMode::BackToFront && camera && camera->valid) {

			const Vector3 cameraPos = camera->cameraPos;
			std::sort(outInstances.begin(), outInstances.end(),
				[&cameraPos](const ParticleInstanceData& lhs, const ParticleInstanceData& rhs) {
					const Vector3 lhsDiff = lhs.worldMatrix.GetTranslationValue() - cameraPos;
					const Vector3 rhsDiff = rhs.worldMatrix.GetTranslationValue() - cameraPos;
					return Vector3::Dot(rhsDiff, rhsDiff) < Vector3::Dot(lhsDiff, lhsDiff);
				});
		}
	}
}

void Engine::ParticleRenderBackend::BuildTrailVertices(const RenderDrawContext& context,
	std::span<const RenderItem* const> items, std::vector<ParticleTrailVertex>& outVertices) const {

	outVertices.clear();
	for (const RenderItem* item : items) {

		const ParticleRenderPayload* payload = context.batch->GetPayload<ParticleRenderPayload>(*item);
		if (!payload || !payload->emitter) {
			continue;
		}
		const ParticleEmitterComponent& emitter = *payload->emitter;
		const ParticleTrailSettings& trail = emitter.runtimeRenderSettings.trail;

		const ResolvedCameraView* camera = context.view ? context.view->FindCamera(item->cameraDomain) : nullptr;
		const Vector3 cameraPos = (camera && camera->valid) ? camera->cameraPos : Vector3::AnyInit(0.0f);
		const float halfWidth = trail.width * 0.5f;

		for (const Particle& particle : emitter.runtimeParticles) {

			auto trailIt = emitter.runtimeTrails.find(particle.id);
			if (trailIt == emitter.runtimeTrails.end()) {
				continue;
			}
			// 記録済みの軌跡点に現在位置を先頭として足してリボンを張る
			const std::vector<Vector3>& points = trailIt->second;
			const Vector3 headPos = particle.position;
			const size_t pointCount = points.size() + 1;
			if (pointCount < 2) {
				continue;
			}

			auto getPoint = [&](size_t index) -> Vector3 {
				return index < points.size() ? points[index] : headPos;
				};

			// 隣接する点をつないだセグメントごとに、視線と直交する方向へ幅を張る
			for (size_t i = 0; i + 1 < pointCount; ++i) {

				const Vector3 p0 = getPoint(i);
				const Vector3 p1 = getPoint(i + 1);
				const Vector3 segment = p1 - p0;
				if (Vector3::Dot(segment, segment) <= 1e-8f) {
					continue;
				}
				const Vector3 viewDir = Vector3::Normalize(cameraPos - p0);
				const Vector3 side = Vector3::Normalize(Vector3::Cross(Vector3::Normalize(segment), viewDir)) * halfWidth;

				// 尻尾ほど透明にする
				const float t0 = static_cast<float>(i) / static_cast<float>(pointCount - 1);
				const float t1 = static_cast<float>(i + 1) / static_cast<float>(pointCount - 1);
				Color4 color0 = particle.color;
				color0.a *= t0;
				Color4 color1 = particle.color;
				color1.a *= t1;

				ParticleTrailVertex v0{};
				v0.position = p0 - side; v0.uv = Vector2(t0, 0.0f); v0.color = color0;
				ParticleTrailVertex v1{};
				v1.position = p0 + side; v1.uv = Vector2(t0, 1.0f); v1.color = color0;
				ParticleTrailVertex v2{};
				v2.position = p1 - side; v2.uv = Vector2(t1, 0.0f); v2.color = color1;
				ParticleTrailVertex v3{};
				v3.position = p1 + side; v3.uv = Vector2(t1, 1.0f); v3.color = color1;

				outVertices.emplace_back(v0);
				outVertices.emplace_back(v1);
				outVertices.emplace_back(v2);
				outVertices.emplace_back(v2);
				outVertices.emplace_back(v1);
				outVertices.emplace_back(v3);
			}
		}
	}
}

void Engine::ParticleRenderBackend::DrawTrails(const RenderDrawContext& context, const RenderItem* item,
	const BackendDrawCommon::ResolvedMaterialPass& resolvedPass, ParticleBatchResources& resources) {

	if (resources.GetTrailVertexCount() == 0) {
		return;
	}
	GraphicsCore& graphicsCore = *context.graphicsCore;
	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();

	// トレイル専用パイプラインで解決する、粒子と同じマテリアルのパラメータとテクスチャを使う
	MaterialPassBinding trailPass{};
	trailPass.passKind = MaterialPassKind::Transparent;
	trailPass.pipeline = BuiltinAssets::Pipelines::ParticleTrail;
	trailPass.preferredVariant = PipelineVariantKind::GraphicsVertex;
	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, trailPass);
	if (!pipelineState) {
		return;
	}

	// viewの定数バッファを確保する
	ParticleViewConstants viewConstants{};
	if (const ResolvedCameraView* camera = context.view->FindCamera(item->cameraDomain); camera && camera->valid) {
		viewConstants.viewProjection = camera->matrices.viewProjectionMatrix;
		viewConstants.cameraPosition = camera->cameraPos;
	}
	const PostProcessConstantBufferAllocation viewAlloc = constantBufferAllocator_.AllocateAndUpload(device, viewConstants);

	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, item->blendMode);

	SyncAndBindRegistry(*pipelineState, context, commandList);
	if (perDrawBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAlloc.gpuAddress);
	}
	if (perDrawBindCache_.Has(trailVerticesSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(trailVerticesSRVSlot_),
			resources.GetTrailVerticesGPUAddress(), {});
	}
	if (resolvedPass.material) {
		BindMaterial(context, *pipelineState, *resolvedPass.material, nullptr, commandList);
	}

	// リボン頂点をそのまま三角形リストとして描画する
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(resources.GetTrailVertexCount(), 1, 0, 0);
}

void Engine::ParticleRenderBackend::DrawBatch(const RenderDrawContext& context,
	std::span<const RenderItem* const> items) {

	GraphicsCore& graphicsCore = *context.graphicsCore;
	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();

	const RenderItem* item = items.front();
	const ParticleRenderPayload* payload = context.batch->GetPayload<ParticleRenderPayload>(*item);
	if (!payload || !payload->emitter || payload->emitter->runtimeParticles.empty()) {
		return;
	}
	const ParticleRenderSettings& settings = payload->emitter->runtimeRenderSettings;

	// エミッターの描画設定からマテリアルを解決する
	const bool is2D = settings.space == PrimitiveRenderSpace::Screen2D;
	BackendDrawCommon::ResolvedMaterialPass resolvedPass{};
	if (!ResolveParticlePass(context, settings.material, is2D, resolvedPass)) {
		return;
	}

	// バッチのインスタンスデータを集めてアップロードする
	std::vector<ParticleInstanceData> instances;
	CollectInstances(context, items, instances);
	ParticleBatchResources& resources = resourcePool_.Acquire(graphicsCore,
		[](ParticleBatchResources& resource, GraphicsCore& core) {
			resource.Init(core);
		});
	resources.UploadInstances(instances);
	if (resources.GetInstanceCount() == 0) {
		return;
	}

	// viewの定数バッファを確保する
	ParticleViewConstants viewConstants{};
	if (const ResolvedCameraView* camera = context.view->FindCamera(item->cameraDomain); camera && camera->valid) {
		viewConstants.viewProjection = camera->matrices.viewProjectionMatrix;
		viewConstants.cameraPosition = camera->cameraPos;
	}
	const PostProcessConstantBufferAllocation viewAlloc = constantBufferAllocator_.AllocateAndUpload(device, viewConstants);

	// 形状アニメはパラメトリックMS、Model粒子はメッシュ、他は共有ジオメトリで描画する
	bool drawn = false;
	if (UseParametricShape(context, settings)) {

		// 専用MSパイプラインを解決する、解決できなければ共有ジオメトリへ落とす
		MaterialPassBinding shapePass{};
		shapePass.passKind = MaterialPassKind::Transparent;
		shapePass.pipeline = settings.shape == PrimitiveType::Ring ?
			BuiltinAssets::Pipelines::ParticleRingMS : BuiltinAssets::Pipelines::ParticleCylinderMS;
		shapePass.preferredVariant = PipelineVariantKind::GraphicsMesh;
		const PipelineVariantDesc* variant = nullptr;
		const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, shapePass, &variant);
		if (pipelineState && variant && variant->kind == PipelineVariantKind::GraphicsMesh) {

			// 分割数の定数バッファを確保する
			ParticleShapeConstants shapeConstants{};
			shapeConstants.divide = static_cast<uint32_t>(std::clamp(
				settings.shape == PrimitiveType::Ring ? settings.ring.divide : settings.cylinder.radialDivide,
				3, kMaxPrimitiveDivide));
			const PostProcessConstantBufferAllocation shapeAlloc = constantBufferAllocator_.AllocateAndUpload(device, shapeConstants);

			ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
				context, *pipelineState, item->blendMode);

			SyncAndBindRegistry(*pipelineState, context, commandList);
			if (perDrawBindCache_.Has(viewCBVSlot_)) {
				RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAlloc.gpuAddress);
			}
			if (perDrawBindCache_.Has(shapeConstantsCBVSlot_)) {
				RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(shapeConstantsCBVSlot_), shapeAlloc.gpuAddress);
			}
			if (perDrawBindCache_.Has(instancesSRVSlot_)) {
				RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(instancesSRVSlot_),
					resources.GetInstancesGPUAddress(), {});
			}
			if (resolvedPass.material) {
				BindMaterial(context, *pipelineState, *resolvedPass.material, nullptr, commandList);
			}

			// 1グループ64三角形で全粒子分をDispatchMeshする
			const uint32_t triangleCount = shapeConstants.divide * 2;
			const uint32_t groupCount = (triangleCount + kParticleMeshGroupTriangles - 1) / kParticleMeshGroupTriangles;
			commandList->DispatchMesh(groupCount, resources.GetInstanceCount(), 1);
			drawn = true;
		}
	} else if (settings.model) {

		// Model粒子、メッシュのGPUリソースを引きインスタンシング描画する
		const MeshGPUResource* meshResource = meshResourceManager_.Find(settings.model);
		if (!meshResource) {

			meshResourceManager_.RequestMesh(*context.assetDatabase, settings.model);
			meshResourceManager_.FlushUploads();
			meshResource = meshResourceManager_.Find(settings.model);
		}
		if (meshResource && meshResource->vertexSRV.buffer && meshResource->indexCount != 0) {

			const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);
			if (pipelineState) {

				ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
					context, *pipelineState, item->blendMode);

				SyncAndBindRegistry(*pipelineState, context, commandList);
				if (perDrawBindCache_.Has(viewCBVSlot_)) {
					RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAlloc.gpuAddress);
				}
				if (perDrawBindCache_.Has(verticesSRVSlot_)) {
					RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(verticesSRVSlot_),
						meshResource->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress(), {});
				}
				if (perDrawBindCache_.Has(instancesSRVSlot_)) {
					RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(instancesSRVSlot_),
						resources.GetInstancesGPUAddress(), {});
				}
				if (resolvedPass.material) {
					BindMaterial(context, *pipelineState, *resolvedPass.material, nullptr, commandList);
				}

				commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				const D3D12_INDEX_BUFFER_VIEW indexBufferView = meshResource->indexBuffer.GetIndexBufferView();
				commandList->IASetIndexBuffer(&indexBufferView);
				commandList->DrawIndexedInstanced(meshResource->indexCount, resources.GetInstanceCount(), 0, 0, 0);
				drawn = true;
			}
		}
	}
	if (!drawn) {

		// 粒子が共有する形状ジオメトリを取得する、無ければ生成する
		const PrimitiveRendererComponent shape = MakeShapeComponent(settings);
		const uint64_t geometryHash = PrimitiveMeshGenerator::ComputeHash(shape);
		const PrimitiveGeometry* geometry = geometryManager_.GetOrCreate(graphicsCore, geometryHash, shape);
		if (!geometry || geometry->indexCount == 0 || !geometry->vertexBuffer.buffer) {
			return;
		}

		const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);
		if (!pipelineState) {
			return;
		}

		ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
			context, *pipelineState, item->blendMode);

		SyncAndBindRegistry(*pipelineState, context, commandList);
		if (perDrawBindCache_.Has(viewCBVSlot_)) {
			RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAlloc.gpuAddress);
		}
		if (perDrawBindCache_.Has(verticesSRVSlot_)) {
			RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(verticesSRVSlot_),
				geometry->vertexBuffer.buffer->GetResource()->GetGPUVirtualAddress(), {});
		}
		if (perDrawBindCache_.Has(instancesSRVSlot_)) {
			RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(instancesSRVSlot_),
				resources.GetInstancesGPUAddress(), {});
		}
		if (resolvedPass.material) {
			BindMaterial(context, *pipelineState, *resolvedPass.material, nullptr, commandList);
		}

		// 共有インデックスバッファでインスタンシング描画する
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		const D3D12_INDEX_BUFFER_VIEW indexBufferView = geometry->indexBuffer.GetIndexBufferView();
		commandList->IASetIndexBuffer(&indexBufferView);
		commandList->DrawIndexedInstanced(geometry->indexCount, resources.GetInstanceCount(), 0, 0, 0);
	}

	// トレイルは3Dのみリボンを構築して重ねて描画する
	if (settings.trail.enabled && !is2D) {

		std::vector<ParticleTrailVertex> trailVertices;
		BuildTrailVertices(context, items, trailVertices);
		resources.UploadTrailVertices(trailVertices);
		DrawTrails(context, item, resolvedPass, resources);
	}
}
