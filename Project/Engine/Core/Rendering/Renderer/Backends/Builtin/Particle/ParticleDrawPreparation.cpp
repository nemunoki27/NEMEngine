#include "ParticleDrawPreparation.h"

//============================================================================
//	include
//============================================================================
#include "ParticleRenderDataUtility.h"
#include "ParticleTrailDataBuilder.h"
#include <Engine/Core/Rendering/Particle/Structures/ParticleShapeDataUtility.h>

#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialCompatibility.h>
#include <Engine/Core/Rendering/Particle/Parametric/ParticleParametricShapeRegistry.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BillboardComponent.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <unordered_map>

//============================================================================
//	ParticleRenderBackend internal
//============================================================================
namespace Engine::ParticleDrawPreparation {

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

	void BuildPhaseMaterialInstance(
		const Engine::ParticlePhaseMaterialSettings& materialSettings,
		Engine::MaterialParameterSet& outInstance) {

		outInstance.clear();
		if (materialSettings.baseColorTexture) {
			Engine::MaterialParameterValue value{};
			value.value = materialSettings.baseColorTexture;
			outInstance.Set(
				Engine::MaterialParameterIDs::BaseColorTexture,
				Engine::MaterialParameterNames::BaseColorTexture,
				Engine::MaterialParameterSemantic::BaseColorTexture,
				value);
		}
		for (const auto& [name, texture] : materialSettings.textureOverrides) {
			if (!texture) {
				continue;
			}
			Engine::MaterialParameterValue value{};
			value.value = texture;
			outInstance.Set(name, value);
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
			outInstance.Set(name, value);
		}
	}

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

	AssetID ResolveNormalTexture(const ParticleRenderSettings& settings,
		const ParticlePhaseMaterialSettings& phase, const MaterialAsset& material) {

		AssetID texture = settings.surface.normalTexture;
		if (!texture) {
			const auto* parameter = material.parameters.FindByName("particleNormalTexture");
			if (parameter) if (const auto* asset = std::get_if<AssetID>(&parameter->value)) texture = *asset;
		}
		const auto override = phase.textureOverrides.find("particleNormalTexture");
		return override != phase.textureOverrides.end() ? override->second : texture;
	}

	bool ResolveGPUParticlePass(const RenderDrawContext& context, AssetID requestedMaterial,
		bool is2D, BackendDrawCommon::ResolvedMaterialPass& outResolved) {

		const AssetID expected = requestedMaterial ? requestedMaterial :
			(is2D ? BuiltinAssets::Materials::DefaultParticle2D : BuiltinAssets::Materials::DefaultParticle);
		if (!ResolveParticlePass(context, requestedMaterial, is2D, outResolved) || outResolved.materialID != expected) return false;
		if (outResolved.material->shaderGraph) return true;
		const AssetID particle2DPipeline{0x4e454d4153534554ull, 0xeff0000000000007ull};
		return !outResolved.pass->shaderOverride && (outResolved.pass->pipeline == BuiltinAssets::Pipelines::DefaultParticle ||
			outResolved.pass->pipeline == particle2DPipeline);
	}

	void CollectParticleInstances(const RenderDrawContext& context,
		std::span<const RenderItem* const> items, const std::vector<ParticleCustomParameterLayout>& customLayouts,
		std::vector<ParticleDrawInstanceData>& outInstances, std::vector<uint32_t>& outPhaseCounts,
		std::vector<uint8_t>& outCustomParameters, std::vector<uint32_t>& outCustomOffsets) {

		outInstances.clear();
		outPhaseCounts.clear();
		outCustomParameters.clear();
		outCustomOffsets.clear();
		if (items.empty()) {
			return;
		}
		// フェーズ数は同じ描画設定を共有するバッチ先頭から決める
		const ParticleRenderPayload* firstPayload = context.batch->GetPayload<ParticleRenderPayload>(*items.front());
		if (!firstPayload || !ResolveParticleRenderEmitter(*items.front(), *firstPayload)) {
			return;
		}
		const size_t phaseCount = customLayouts.size();
		if (phaseCount == 0) {
			return;
		}
		outPhaseCounts.resize(phaseCount, 0);
		for (const auto* item : items) {
			const auto* payload = context.batch->GetPayload<ParticleRenderPayload>(*item);
			const auto* emitter = payload ? ResolveParticleRenderEmitter(*item, *payload) : nullptr;
			if (!emitter || (emitter->renderSettings.trail.enabled && !emitter->renderSettings.trail.drawSource)) continue;
			for (const auto particle : emitter->particles) ++outPhaseCounts[(std::min)(size_t(particle.phaseIndex), phaseCount - 1)];
		}
		// Phaseごとの領域を先に決め、粒子単位の動的確保を避ける
		std::vector<uint32_t> phaseOffsets(phaseCount);
		uint32_t total = 0;
		uint32_t customBytes = 0;
		for (size_t phase = 0; phase < phaseCount; ++phase) {
			phaseOffsets[phase] = total;
			outCustomOffsets.push_back(customBytes);
			total += outPhaseCounts[phase];
			customBytes += outPhaseCounts[phase] * customLayouts[phase].stride;
		}
		outInstances.resize(total);
		outCustomParameters.resize(customBytes, 0);
		auto cursors = phaseOffsets;

		for (const RenderItem* item : items) {

			const ParticleRenderPayload* payload = context.batch->GetPayload<ParticleRenderPayload>(*item);
			const ParticleEmitterRuntime* group = payload ?
				ResolveParticleRenderEmitter(*item, *payload) : nullptr;
			if (!group) {
				continue;
			}
			const ParticleRenderSettings& settings = group->renderSettings;
			if (settings.trail.enabled && !settings.trail.drawSource) {
				continue;
			}

			// ビルボードは描画中のビューのカメラへ向ける、BillboardComponentと同じ軸マスク方式
			const ResolvedCameraView* camera = context.view ? context.view->FindCamera(item->cameraDomain) : nullptr;
			const bool useBillboard = settings.space == PrimitiveRenderSpace::World3D &&
				!settings.billboardAxes.empty() && camera && camera->valid;
			BillboardComponent axisMask{};
			axisMask.axisMask = 0;
			for (Axis axis : settings.billboardAxes) {
				SetBillboardAxis(axisMask, axis, true);
			}

			for (const Particle& particle : group->particles) {

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

				const size_t phaseIndex = (std::min)(static_cast<size_t>(particle.phaseIndex), phaseCount - 1);
				const uint32_t instanceIndex = cursors[phaseIndex]++;
				auto& instance = outInstances[instanceIndex];
				instance.geometry.worldMatrix = Matrix4x4::MakeAffineMatrix(
					Vector3::AnyInit(particle.size) * particle.worldScale, rotation, worldPos);
				instance.geometry.vertexColor = particle.color;
				const ParticleShapeData shapeData = settings.shapeOverLifetime ?
					particle.shapeData : MakeParticleShapeData(settings);
				instance.geometry.shapeParams0 = shapeData.params0;
				instance.geometry.shapeParams1 = shapeData.params1;
				instance.geometry.topColor = shapeData.topColor;
				instance.geometry.centerColor = shapeData.centerColor;
				instance.geometry.bottomColor = shapeData.bottomColor;
				instance.material.emissive = particle.emissive;
				const bool flipScreenV = settings.space == PrimitiveRenderSpace::Screen2D &&
					!settings.model && settings.shape == PrimitiveType::Plane;
				instance.material.materialParams = Vector4(
					particle.alphaReference, flipScreenV ? 1.0f : 0.0f,
					static_cast<float>(item->blendMode), 0.0f);
				// フェーズのマテリアル別に描くため、フェーズごとに分けて詰める
				const ParticlePhaseMaterialSettings& materialSettings = GetPhaseMaterialSettings(settings, phaseIndex);
				const float phaseT = particle.lifetime <= 0.0f ?
					1.0f : (std::clamp)(particle.age / particle.lifetime, 0.0f, 1.0f);
				instance.material.materialColor = Color4::White();
				instance.material.uvMatrix = BuildParticleUVMatrix(particle);
				const auto velocity = particle.hasParent ? Vector3::Transform(particle.velocity, particle.parentMatrix) -
					Vector3::Transform(Vector3::AnyInit(0.0f), particle.parentMatrix) : particle.velocity;
				instance.material.velocityAge = Vector4(velocity.x, velocity.y, velocity.z, particle.age);
				instance.material.lifetimeRandom = Vector4(particle.lifetime, phaseT, ParticleStableRandom(particle.id), 0);
				instance.material.particleColor = Vector4(particle.color.r, particle.color.g, particle.color.b, particle.color.a);
				if (phaseIndex < settings.phaseCustomData.size() && settings.phaseCustomData[phaseIndex].enabled) {
					const auto& customData = settings.phaseCustomData[phaseIndex];
					instance.material.customData1 = EvaluateParticleMaterialParameter(customData.customData1, phaseT);
					instance.material.customData2 = EvaluateParticleMaterialParameter(customData.customData2, phaseT);
				}
				const ParticleCustomParameterLayout& customLayout = customLayouts[phaseIndex];
				auto custom = std::span(outCustomParameters).subspan(outCustomOffsets[phaseIndex] +
					(instanceIndex - phaseOffsets[phaseIndex]) * customLayout.stride, customLayout.stride);
				std::copy_n(customLayout.defaultData.begin(), (std::min)(customLayout.defaultData.size(), custom.size()), custom.begin());
				for (const ShaderConstantBufferVariable& variable : customLayout.variables) {

					auto parameter = materialSettings.parameters.find(variable.name);
					if (parameter == materialSettings.parameters.end()) {
						continue;
					}
					WriteParticleCustomParameter(custom, variable,
						EvaluateParticleMaterialParameter(parameter->second, phaseT));
				}
			}
		}
	}
}


Engine::PrimitiveRendererComponent Engine::ParticleDrawPreparation::MakeShapeComponent(const Engine::ParticleRenderSettings& settings) {

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
