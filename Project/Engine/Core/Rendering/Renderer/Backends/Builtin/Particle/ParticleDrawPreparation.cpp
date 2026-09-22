#include "ParticleDrawPreparation.h"

//============================================================================
//	include
//============================================================================
#include "ParticleRenderDataUtility.h"
#include "ParticleTrailDataBuilder.h"

#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
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

	Engine::ParticleShapeData MakeStaticShapeData(const Engine::ParticleRenderSettings& settings) {

		Engine::ParticleShapeData data{};
		if (settings.shape == Engine::PrimitiveType::Ring) {

			data.params0 = Engine::Vector4(
				settings.ring.outerRadius,
				settings.ring.innerRadius,
				settings.ring.startAngle * Math::radian,
				settings.ring.endAngle * Math::radian);
		} else if (settings.shape == Engine::PrimitiveType::Cylinder) {

			const Engine::PrimitiveCylinderParams& cylinder = settings.cylinder;
			data.params0 = Engine::Vector4(
				cylinder.topRadius, cylinder.centerRadius, cylinder.bottomRadius, cylinder.height);
			data.params1 = Engine::Vector4(
				cylinder.topRadiusWeight, cylinder.bottomRadiusWeight,
				cylinder.maxAngle * Math::radian, 1.0f);
			data.topColor = cylinder.topColor;
			data.centerColor = cylinder.centerColor;
			data.bottomColor = cylinder.bottomColor;
		}
		return data;
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
		// フェーズ数は同じアセットを共有するバッチ先頭から決める
		const ParticleRenderPayload* firstPayload = context.batch->GetPayload<ParticleRenderPayload>(*items.front());
		if (!firstPayload || !ResolveParticleRenderGroup(*items.front(), *firstPayload)) {
			return;
		}
		const size_t phaseCount = customLayouts.size();
		if (phaseCount == 0) {
			return;
		}
		std::vector<std::vector<ParticleDrawInstanceData>> phaseBuckets(phaseCount);

		for (const RenderItem* item : items) {

			const ParticleRenderPayload* payload = context.batch->GetPayload<ParticleRenderPayload>(*item);
			const ParticleGroupRuntimeState* group = payload ?
				ResolveParticleRenderGroup(*item, *payload) : nullptr;
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

				ParticleDrawInstanceData instance{};
				instance.geometry.worldMatrix = Matrix4x4::MakeAffineMatrix(
					Vector3::AnyInit(particle.size) * particle.worldScale, rotation, worldPos);
				instance.geometry.vertexColor = particle.color;
				const ParticleShapeData shapeData = settings.shapeOverLifetime ?
					particle.shapeData : MakeStaticShapeData(settings);
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
				const size_t phaseIndex = (std::min)(static_cast<size_t>(particle.phaseIndex), phaseCount - 1);
				const ParticlePhaseMaterialSettings& materialSettings = GetPhaseMaterialSettings(settings, phaseIndex);
				const float phaseT = particle.lifetime <= 0.0f ?
					1.0f : (std::clamp)(particle.age / particle.lifetime, 0.0f, 1.0f);
				instance.material.materialColor = Color4::White();
				instance.material.uvMatrix = BuildParticleUVMatrix(particle);
				const ParticleCustomParameterLayout& customLayout = customLayouts[phaseIndex];
				instance.customParameters = customLayout.defaultData;
				instance.customParameters.resize(customLayout.stride, 0);
				for (const ShaderConstantBufferVariable& variable : customLayout.variables) {

					auto parameter = materialSettings.parameters.find(variable.name);
					if (parameter == materialSettings.parameters.end()) {
						continue;
					}
					WriteParticleCustomParameter(instance.customParameters, variable,
						EvaluateParticleMaterialParameter(parameter->second, phaseT));
				}
				phaseBuckets[phaseIndex].emplace_back(instance);
			}
		}

		// フェーズ順に連結する
		for (std::vector<ParticleDrawInstanceData>& bucket : phaseBuckets) {
			outCustomOffsets.emplace_back(static_cast<uint32_t>(outCustomParameters.size()));
			outPhaseCounts.emplace_back(static_cast<uint32_t>(bucket.size()));
			for (const ParticleDrawInstanceData& instance : bucket) {
				outCustomParameters.insert(outCustomParameters.end(),
					instance.customParameters.begin(), instance.customParameters.end());
			}
			outInstances.insert(outInstances.end(), bucket.begin(), bucket.end());
		}
	}
}
