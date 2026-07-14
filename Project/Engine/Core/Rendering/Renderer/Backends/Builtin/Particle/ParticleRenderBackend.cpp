#include "ParticleRenderBackend.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialCompatibility.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/World/Components/Rendering/ParticleEmitterComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BillboardComponent.h>

// c++
#include <algorithm>
#include <cstring>
#include <unordered_map>

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

	// 粒子のUVパラメータから変換行列を生成
	Engine::Matrix4x4 BuildParticleUVMatrix(const Engine::Particle& particle) {

		const Engine::Vector3 scale(particle.uvScale.x, particle.uvScale.y, 1.0f);
		const Engine::Vector3 rotation(0.0f, 0.0f, particle.uvRotation);
		const Engine::Vector3 translation(particle.uvOffset.x, particle.uvOffset.y, 0.0f);
		const Engine::Matrix4x4 scaleRotation = Engine::Matrix4x4::MakeAffineMatrix(
			scale, rotation, Engine::Vector3::AnyInit(0.0f));
		const Engine::Vector3 pivot(particle.uvPivot.x, particle.uvPivot.y, 0.0f);
		const Engine::Vector3 translationWithPivot =
			translation + pivot - Engine::Vector3::Transform(pivot, scaleRotation);
		return Engine::Matrix4x4::MakeAffineMatrix(scale, rotation, translationWithPivot);
	}

	const Engine::ParticlePhaseMaterialSettings& GetPhaseMaterialSettings(
		const Engine::ParticleRenderSettings& settings, size_t phaseIndex) {

		static const Engine::ParticlePhaseMaterialSettings kDefault{};
		if (phaseIndex < settings.phaseMaterialSettings.size()) {
			return settings.phaseMaterialSettings[phaseIndex];
		}
		return kDefault;
	}

	Engine::ParticleCustomParameterLayout BuildCustomParameterLayout(
		const Engine::ShaderReflectionInfo& reflection) {

		Engine::ParticleCustomParameterLayout layout{};
		const Engine::ShaderStructuredBufferInfo* buffer =
			Engine::FindStructuredBuffer(reflection, "gParticleCustomParameters");
		if (!buffer || buffer->stride == 0) {
			return layout;
		}
		layout.stride = buffer->stride;
		for (const Engine::ShaderConstantBufferVariable& variable : buffer->variables) {
			if (variable.valueType == D3D_SVT_FLOAT && variable.offset < layout.stride) {
				layout.variables.emplace_back(variable);
			}
		}
		return layout;
	}

	void WriteCustomParameter(std::vector<uint8_t>& data,
		const Engine::ShaderConstantBufferVariable& variable, const Engine::Vector4& value) {

		const uint32_t componentCount = Engine::GetVariableComponentCount(variable);
		const uint32_t writeSize = (std::min)(componentCount * static_cast<uint32_t>(sizeof(float)),
			variable.size);
		if (writeSize == 0 || variable.offset + writeSize > data.size()) {
			return;
		}
		const float values[4] = { value.x, value.y, value.z, value.w };
		std::memcpy(data.data() + variable.offset, values, writeSize);
	}

	void AppendPhaseMaterialOverrides(const Engine::ParticlePhaseMaterialSettings& materialSettings,
		std::unordered_map<std::string, Engine::MaterialParameterValue>& outOverrides) {

		outOverrides.clear();
		if (materialSettings.baseColorTexture) {
			Engine::MaterialParameterValue value{};
			value.value = materialSettings.baseColorTexture;
			outOverrides["baseColorTexture"] = value;
		}
		for (const auto& [name, texture] : materialSettings.textureOverrides) {
			if (!texture) {
				continue;
			}
			Engine::MaterialParameterValue value{};
			value.value = texture;
			outOverrides[name] = value;
		}
		for (const auto& [name, parameter] : materialSettings.parameters) {
			if (parameter.mode != Engine::ParticleMaterialParameterMode::Constant) {
				continue;
			}
			Engine::MaterialParameterValue value{};
			if (parameter.componentCount <= 1) {
				value.value = parameter.constant.x;
			} else if (parameter.componentCount == 2) {
				value.value = Engine::Vector2(parameter.constant.x, parameter.constant.y);
			} else if (parameter.componentCount == 3) {
				value.value = Engine::Vector3(parameter.constant.x, parameter.constant.y, parameter.constant.z);
			} else {
				value.value = parameter.constant;
			}
			outOverrides[name] = value;
		}
	}

	// エフェクトのマテリアルを解決する、未設定は描画空間に応じたビルトインの既定マテリアルへ落とす
	bool ResolveParticlePass(const Engine::RenderDrawContext& context, Engine::AssetID requestedMaterial,
		bool is2D, Engine::BackendDrawCommon::ResolvedMaterialPass& outResolved) {

		const Engine::AssetID defaultMaterial = is2D ?
			Engine::BuiltinAssets::Materials::DefaultParticle2D : Engine::BuiltinAssets::Materials::DefaultParticle;
		auto tryResolve = [&](Engine::AssetID materialID) {

			const Engine::MaterialAsset* material = context.assetLibrary->LoadMaterial(materialID);
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
			const Engine::PipelineState* pipeline = Engine::BackendDrawCommon::ResolveGraphicsPipeline(
				context, *pass);
			if (!pipeline || !Engine::CheckParticleMaterialCompatibility(
				*material, &pipeline->GetGraphicsReflection()).IsCompatible()) {
				return false;
			}

			outResolved.materialID = materialID;
			outResolved.material = material;
			outResolved.pass = pass;
			return true;
		};

		const Engine::AssetID materialID = requestedMaterial ? requestedMaterial : defaultMaterial;
		if (tryResolve(materialID)) {
			return true;
		}
		return materialID != defaultMaterial && tryResolve(defaultMaterial);
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

	// 形状アニメのパラメトリックMS生成を解決する、MS対応GPUかつ登録済み形状のみ
	const Engine::IParticleParametricShape* ResolveParametricShape(
		const Engine::RenderDrawContext& context, const Engine::ParticleRenderSettings& settings) {

		if (!settings.shapeOverLifetime || settings.model ||
			settings.space == Engine::PrimitiveRenderSpace::Screen2D) {
			return nullptr;
		}
		if (!context.runtimeFeatures.useMeshShader || context.forceVertexMeshVariant) {
			return nullptr;
		}
		return Engine::ParticleParametricShapeRegistry::GetInstance().Find(settings.shape);
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
	std::span<const RenderItem* const> items, const std::vector<ParticleCustomParameterLayout>& customLayouts,
	std::vector<ParticleDrawInstanceData>& outInstances, std::vector<uint32_t>& outPhaseCounts,
	std::vector<uint8_t>& outCustomParameters, std::vector<uint32_t>& outCustomOffsets) const {

	outInstances.clear();
	outPhaseCounts.clear();
	outCustomParameters.clear();
	outCustomOffsets.clear();
	if (items.empty()) {
		return;
	}
	// フェーズ数は同じアセットを共有するバッチ先頭から決める
	const ParticleRenderPayload* firstPayload = context.batch->GetPayload<ParticleRenderPayload>(*items.front());
	if (!firstPayload || !firstPayload->emitter) {
		return;
	}
	const size_t phaseCount = customLayouts.size();
	if (phaseCount == 0) {
		return;
	}
	std::vector<std::vector<ParticleDrawInstanceData>> phaseBuckets(phaseCount);
	bool sortBackToFront = false;
	Vector3 sortCameraPos = Vector3::AnyInit(0.0f);

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
		if (settings.sortMode == ParticleSortMode::BackToFront && camera && camera->valid) {

			sortBackToFront = true;
			sortCameraPos = camera->cameraPos;
		}

		for (const Particle& particle : payload->emitter->runtimeParticles) {

			// 親ローカルのシミュレーション結果から確定したワールド姿勢を使う
			const Vector3 worldPos = particle.worldPos;
			// 粒子の回転を掛けてからカメラへ向ける
			Quaternion rotation = particle.worldRotation;
			if (useBillboard) {

				const Quaternion desired = RenderBillboard::MakeCameraBillboardRotation(*camera, worldPos);
				if (settings.billboardAxes.size() == 3) {
					rotation = desired * particle.worldRotation;
				} else {

					// 一部軸のみのビルボードは軸マスクで合成する
					const Vector3 localForward = Vector3::NormalizeOr(Vector3::Transform(
						Vector3(0.0f, 0.0f, 1.0f), Quaternion::MakeRotateMatrix(desired)), Vector3(0.0f, 0.0f, 1.0f));
					rotation = RenderBillboard::ApplyAxisMask(particle.worldRotation, desired, axisMask, localForward);
				}
			}

			ParticleDrawInstanceData instance{};
			instance.geometry.worldMatrix = Matrix4x4::MakeAffineMatrix(
				Vector3::AnyInit(particle.size) * particle.worldScale, rotation, worldPos);
			instance.geometry.vertexColor = particle.color;
			instance.geometry.shapeParams = particle.shapeParams;
			instance.material.emissive = particle.emissive;
			instance.material.materialParams = Vector4(particle.alphaReference, 0.0f, 0.0f, 0.0f);
			// フェーズのマテリアル別に描くため、フェーズごとに分けて詰める
			const size_t phaseIndex = (std::min)(static_cast<size_t>(particle.phaseIndex), phaseCount - 1);
			const ParticlePhaseMaterialSettings& materialSettings = GetPhaseMaterialSettings(settings, phaseIndex);
			const float phaseT = particle.lifetime <= 0.0f ?
				1.0f : (std::clamp)(particle.age / particle.lifetime, 0.0f, 1.0f);
			instance.material.materialColor = Color4::White();
			instance.material.uvMatrix = BuildParticleUVMatrix(particle);
			const ParticleCustomParameterLayout& customLayout = customLayouts[phaseIndex];
			instance.customParameters.resize(customLayout.stride, 0);
			for (const ShaderConstantBufferVariable& variable : customLayout.variables) {

				auto parameter = materialSettings.parameters.find(variable.name);
				if (parameter == materialSettings.parameters.end()) {
					continue;
				}
				WriteCustomParameter(instance.customParameters, variable,
					EvaluateParticleMaterialParameter(parameter->second, phaseT));
			}
			phaseBuckets[phaseIndex].emplace_back(instance);
		}
	}

	// フェーズ順に連結し、ソートはフェーズ範囲内で行う
	for (std::vector<ParticleDrawInstanceData>& bucket : phaseBuckets) {

		// 半透明の重なりを正しく見せるため、奥から手前の順へ並べ替える
		if (sortBackToFront) {
			std::sort(bucket.begin(), bucket.end(),
				[&sortCameraPos](const ParticleDrawInstanceData& lhs, const ParticleDrawInstanceData& rhs) {
					const Vector3 lhsDiff = lhs.geometry.worldMatrix.GetTranslationValue() - sortCameraPos;
					const Vector3 rhsDiff = rhs.geometry.worldMatrix.GetTranslationValue() - sortCameraPos;
					return Vector3::Dot(rhsDiff, rhsDiff) < Vector3::Dot(lhsDiff, lhsDiff);
				});
		}
		outCustomOffsets.emplace_back(static_cast<uint32_t>(outCustomParameters.size()));
		outPhaseCounts.emplace_back(static_cast<uint32_t>(bucket.size()));
		for (const ParticleDrawInstanceData& instance : bucket) {
			outCustomParameters.insert(outCustomParameters.end(),
				instance.customParameters.begin(), instance.customParameters.end());
		}
		outInstances.insert(outInstances.end(), bucket.begin(), bucket.end());
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

		for (const Particle& particle : emitter.runtimeParticles) {

			auto trailIt = emitter.runtimeTrails.find(particle.id);
			if (trailIt == emitter.runtimeTrails.end()) {
				continue;
			}
			// 記録済みの軌跡点に現在位置を先頭として足してリボンを張る
			const std::vector<ParticleTrailPoint>& points = trailIt->second;
			const Vector3 headPos = particle.worldPos;
			const size_t pointCount = points.size() + 1;
			if (pointCount < 2) {
				continue;
			}

			auto getPoint = [&](size_t index) -> Vector3 {
				return index < points.size() ? points[index].position : headPos;
				};
			// 尻尾から先頭へ、リボンの色と幅を進行度で補間する
			auto ribbonColor = [&](float t) {
				return Color4::Lerp(trail.endColor, trail.startColor, t) * particle.color;
				};
			auto ribbonHalfWidth = [&](float t) {
				return Math::Lerp(trail.endWidth, trail.startWidth, t) * 0.5f;
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
				const Vector3 side = Vector3::Normalize(Vector3::Cross(Vector3::Normalize(segment), viewDir));

				const float t0 = static_cast<float>(i) / static_cast<float>(pointCount - 1);
				const float t1 = static_cast<float>(i + 1) / static_cast<float>(pointCount - 1);
				const Color4 color0 = ribbonColor(t0);
				const Color4 color1 = ribbonColor(t1);
				const Vector3 side0 = side * ribbonHalfWidth(t0);
				const Vector3 side1 = side * ribbonHalfWidth(t1);

				ParticleTrailVertex v0{};
				v0.position = p0 - side0; v0.uv = Vector2(t0, 0.0f); v0.color = color0;
				ParticleTrailVertex v1{};
				v1.position = p0 + side0; v1.uv = Vector2(t0, 1.0f); v1.color = color0;
				ParticleTrailVertex v2{};
				v2.position = p1 - side1; v2.uv = Vector2(t1, 0.0f); v2.color = color1;
				ParticleTrailVertex v3{};
				v3.position = p1 + side1; v3.uv = Vector2(t1, 1.0f); v3.color = color1;

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

	// トレイルVSとMaterialのPS、描画状態を合成する
	const PipelineState* pipelineState = BackendDrawCommon::ResolveComposedGraphicsPipeline(
		context, *resolvedPass.pass, BuiltinAssets::Pipelines::ParticleTrail,
		PipelineVariantKind::GraphicsVertex);
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
	if (perDrawBindCache_.Has(materialsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(materialsSRVSlot_),
			resources.GetMaterialsGPUAddress(), {});
	}
	if (perDrawBindCache_.Has(customParametersSRVSlot_) && resources.GetCustomParametersGPUAddress() != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(customParametersSRVSlot_),
			resources.GetCustomParametersGPUAddress(), {});
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

	const bool is2D = settings.space == PrimitiveRenderSpace::Screen2D;
	const size_t phaseCount = (std::max)((std::max)(settings.phaseMaterials.size(),
		settings.phaseMaterialSettings.size()), static_cast<size_t>(1));
	std::vector<BackendDrawCommon::ResolvedMaterialPass> phasePasses(phaseCount);
	std::vector<ParticleCustomParameterLayout> customLayouts(phaseCount);
	for (size_t phaseIndex = 0; phaseIndex < phaseCount; ++phaseIndex) {

		const AssetID phaseMaterial = (phaseIndex < settings.phaseMaterials.size() && settings.phaseMaterials[phaseIndex]) ?
			settings.phaseMaterials[phaseIndex] : settings.material;
		if (!ResolveParticlePass(context, phaseMaterial, is2D, phasePasses[phaseIndex])) {
			continue;
		}
		const PipelineState* reflectionPipeline = BackendDrawCommon::ResolveGraphicsPipeline(
			context, *phasePasses[phaseIndex].pass);
		if (reflectionPipeline) {
			customLayouts[phaseIndex] = BuildCustomParameterLayout(reflectionPipeline->GetGraphicsReflection());
		}
	}

	// バッチのインスタンスデータをフェーズごとに集めてアップロードする
	std::vector<ParticleDrawInstanceData> instances;
	std::vector<uint32_t> phaseCounts;
	std::vector<uint8_t> customParameters;
	std::vector<uint32_t> customOffsets;
	CollectInstances(context, items, customLayouts, instances, phaseCounts, customParameters, customOffsets);
	ParticleBatchResources& resources = resourcePool_.Acquire(graphicsCore,
		[](ParticleBatchResources& resource, GraphicsCore& core) {
			resource.Init(core);
		});
	resources.UploadInstances(instances);
	resources.UploadCustomParameters(customParameters);
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

	// フェーズごとにマテリアルを解決して連続範囲を描画する、未設定はエフェクト共通へ落とす
	// 形状アニメはパラメトリックMS、Model粒子はメッシュ、他は共有ジオメトリで描画する
	const IParticleParametricShape* parametric = ResolveParametricShape(context, settings);
	uint32_t instanceOffset = 0;
	for (size_t phaseIndex = 0; phaseIndex < phaseCounts.size(); ++phaseIndex) {

		const uint32_t instanceCount = phaseCounts[phaseIndex];
		if (instanceCount == 0) {
			continue;
		}
		const BackendDrawCommon::ResolvedMaterialPass& phasePass = phasePasses[phaseIndex];
		if (!phasePass.pass || !phasePass.material) {

			instanceOffset += instanceCount;
			continue;
		}
		std::unordered_map<std::string, MaterialParameterValue> phaseOverrides{};
		AppendPhaseMaterialOverrides(GetPhaseMaterialSettings(settings, phaseIndex), phaseOverrides);
		const std::unordered_map<std::string, MaterialParameterValue>* phaseOverridePtr =
			phaseOverrides.empty() ? nullptr : &phaseOverrides;
		const D3D12_GPU_VIRTUAL_ADDRESS geometryAddress = resources.GetGeometryGPUAddress() +
			static_cast<uint64_t>(instanceOffset) * sizeof(ParticleGeometryData);
		const D3D12_GPU_VIRTUAL_ADDRESS materialsAddress = resources.GetMaterialsGPUAddress() +
			static_cast<uint64_t>(instanceOffset) * sizeof(ParticleMaterialData);
		const D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress =
			customLayouts[phaseIndex].stride == 0 ? 0 :
			resources.GetCustomParametersGPUAddress() + customOffsets[phaseIndex];

		bool drawn = false;
		if (parametric) {
			drawn = DrawParametricShapePath(context, item, *parametric, settings, phasePass,
				phaseOverridePtr, geometryAddress, materialsAddress, customParametersAddress,
				instanceCount, viewAlloc.gpuAddress);
		}
		if (!drawn && settings.model) {
			drawn = DrawModelMeshPath(context, item, settings, phasePass,
				phaseOverridePtr, geometryAddress, materialsAddress, customParametersAddress,
				instanceCount, viewAlloc.gpuAddress);
		}
		if (!drawn) {
			DrawSharedGeometryPath(context, item, settings, phasePass,
				phaseOverridePtr, geometryAddress, materialsAddress, customParametersAddress,
				instanceCount, viewAlloc.gpuAddress);
		}
		instanceOffset += instanceCount;
	}

	// トレイルは3Dのみリボンを構築して重ねて描画する、専用マテリアル未設定は粒子と同じものを使う
	if (settings.trail.enabled && !is2D) {

		const AssetID trailMaterial = settings.trail.material ? settings.trail.material : settings.material;
		BackendDrawCommon::ResolvedMaterialPass trailPass{};
		if (ResolveParticlePass(context, trailMaterial, is2D, trailPass)) {

			std::vector<ParticleTrailVertex> trailVertices;
			BuildTrailVertices(context, items, trailVertices);
			resources.UploadTrailVertices(trailVertices);
			DrawTrails(context, item, trailPass, resources);
		}
	}
}

bool Engine::ParticleRenderBackend::DrawParametricShapePath(const RenderDrawContext& context, const RenderItem* item,
	const IParticleParametricShape& parametric, const ParticleRenderSettings& settings,
	const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
	const std::unordered_map<std::string, MaterialParameterValue>* materialOverrides,
	D3D12_GPU_VIRTUAL_ADDRESS geometryAddress, D3D12_GPU_VIRTUAL_ADDRESS materialsAddress,
	D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress,
	uint32_t instanceCount,
	D3D12_GPU_VIRTUAL_ADDRESS viewAddress) {

	// 専用MSパイプラインを解決する、解決できなければ共有ジオメトリへ落とす
	const PipelineVariantDesc* variant = nullptr;
	const PipelineState* pipelineState = BackendDrawCommon::ResolveComposedGraphicsPipeline(
		context, *resolvedPass.pass, parametric.GetPipeline(), PipelineVariantKind::GraphicsMesh, &variant);
	if (!pipelineState || !variant || variant->kind != PipelineVariantKind::GraphicsMesh) {
		return false;
	}

	// 分割数の定数バッファを確保する
	ID3D12Device* device = context.graphicsCore->GetDXObject().GetDevice();
	ParticleShapeConstants shapeConstants{};
	shapeConstants.divide = static_cast<uint32_t>(std::clamp(
		parametric.GetDivide(settings), 3, kMaxPrimitiveDivide));
	const PostProcessConstantBufferAllocation shapeAlloc = constantBufferAllocator_.AllocateAndUpload(device, shapeConstants);

	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(
		context, *pipelineState, item->blendMode);

	SyncAndBindRegistry(*pipelineState, context, commandList);
	if (perDrawBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAddress);
	}
	if (perDrawBindCache_.Has(shapeConstantsCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(shapeConstantsCBVSlot_), shapeAlloc.gpuAddress);
	}
	if (perDrawBindCache_.Has(geometrySRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(geometrySRVSlot_), geometryAddress, {});
	}
	if (perDrawBindCache_.Has(materialsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(materialsSRVSlot_), materialsAddress, {});
	}
	if (perDrawBindCache_.Has(customParametersSRVSlot_) && customParametersAddress != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(customParametersSRVSlot_),
			customParametersAddress, {});
	}
	if (resolvedPass.material) {
		BindMaterial(context, *pipelineState, *resolvedPass.material, materialOverrides, commandList);
	}

	// 1グループ64三角形で全粒子分をDispatchMeshする
	const uint32_t triangleCount = shapeConstants.divide * 2;
	const uint32_t groupCount = (triangleCount + kParticleMeshGroupTriangles - 1) / kParticleMeshGroupTriangles;
	commandList->DispatchMesh(groupCount, instanceCount, 1);
	return true;
}

bool Engine::ParticleRenderBackend::DrawModelMeshPath(const RenderDrawContext& context, const RenderItem* item,
	const ParticleRenderSettings& settings,
	const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
	const std::unordered_map<std::string, MaterialParameterValue>* materialOverrides,
	D3D12_GPU_VIRTUAL_ADDRESS geometryAddress, D3D12_GPU_VIRTUAL_ADDRESS materialsAddress,
	D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress,
	uint32_t instanceCount,
	D3D12_GPU_VIRTUAL_ADDRESS viewAddress) {

	// Model粒子、メッシュのGPUリソースを引きインスタンシング描画する
	const MeshGPUResource* meshResource = meshResourceManager_.Find(settings.model);
	if (!meshResource) {

		meshResourceManager_.RequestMesh(*context.assetDatabase, settings.model);
		meshResourceManager_.FlushUploads();
		meshResource = meshResourceManager_.Find(settings.model);
	}
	if (!meshResource || !meshResource->vertexSRV.buffer || meshResource->indexCount == 0) {
		return false;
	}

	const PipelineState* pipelineState = BackendDrawCommon::ResolveGraphicsPipeline(context, *resolvedPass.pass);
	if (!pipelineState) {
		return false;
	}

	ID3D12GraphicsCommandList6* commandList = BackendDrawCommon::SetupGraphicsPipeline(context, *pipelineState, item->blendMode);

	SyncAndBindRegistry(*pipelineState, context, commandList);
	if (perDrawBindCache_.Has(viewCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAddress);
	}
	if (perDrawBindCache_.Has(verticesSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(verticesSRVSlot_),
			meshResource->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress(), {});
	}
	if (perDrawBindCache_.Has(geometrySRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(geometrySRVSlot_), geometryAddress, {});
	}
	if (perDrawBindCache_.Has(materialsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(materialsSRVSlot_), materialsAddress, {});
	}
	if (perDrawBindCache_.Has(customParametersSRVSlot_) && customParametersAddress != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(customParametersSRVSlot_),
			customParametersAddress, {});
	}
	if (resolvedPass.material) {
		BindMaterial(context, *pipelineState, *resolvedPass.material, materialOverrides, commandList);
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_INDEX_BUFFER_VIEW indexBufferView = meshResource->indexBuffer.GetIndexBufferView();
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->DrawIndexedInstanced(meshResource->indexCount, instanceCount, 0, 0, 0);
	return true;
}

void Engine::ParticleRenderBackend::DrawSharedGeometryPath(const RenderDrawContext& context, const RenderItem* item,
	const ParticleRenderSettings& settings,
	const BackendDrawCommon::ResolvedMaterialPass& resolvedPass,
	const std::unordered_map<std::string, MaterialParameterValue>* materialOverrides,
	D3D12_GPU_VIRTUAL_ADDRESS geometryAddress, D3D12_GPU_VIRTUAL_ADDRESS materialsAddress,
	D3D12_GPU_VIRTUAL_ADDRESS customParametersAddress,
	uint32_t instanceCount,
	D3D12_GPU_VIRTUAL_ADDRESS viewAddress) {

	// 粒子が共有する形状ジオメトリを取得する、無ければ生成する
	GraphicsCore& graphicsCore = *context.graphicsCore;
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
		RootBindingCommand::SetGraphicsCBV(commandList, perDrawBindCache_.Get(viewCBVSlot_), viewAddress);
	}
	if (perDrawBindCache_.Has(verticesSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(verticesSRVSlot_),
			geometry->vertexBuffer.buffer->GetResource()->GetGPUVirtualAddress(), {});
	}
	if (perDrawBindCache_.Has(geometrySRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(geometrySRVSlot_), geometryAddress, {});
	}
	if (perDrawBindCache_.Has(materialsSRVSlot_)) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(materialsSRVSlot_), materialsAddress, {});
	}
	if (perDrawBindCache_.Has(customParametersSRVSlot_) && customParametersAddress != 0) {
		RootBindingCommand::SetGraphicsSRV(commandList, perDrawBindCache_.Get(customParametersSRVSlot_),
			customParametersAddress, {});
	}
	if (resolvedPass.material) {
		BindMaterial(context, *pipelineState, *resolvedPass.material, materialOverrides, commandList);
	}

	// 共有インデックスバッファでインスタンシング描画する
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_INDEX_BUFFER_VIEW indexBufferView = geometry->indexBuffer.GetIndexBufferView();
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->DrawIndexedInstanced(geometry->indexCount, instanceCount, 0, 0, 0);
}
