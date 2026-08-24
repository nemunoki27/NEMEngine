#include "ParticleTrailDataBuilder.h"

//============================================================================
//	include
//============================================================================
#include "ParticleRenderDataUtility.h"

#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/ParticleEffectAsset.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleTrailDataBuilder internal
//============================================================================
namespace {

	struct EvaluatedTrailPoint {

		Engine::ParticleTrailPointData render{};
		float lifetimeT = 0.0f;
		float age = 0.0f;
		uint32_t phaseIndex = 0;
	};

	// フェーズ番号からトレイル設定を取得する
	const Engine::ParticleTrailPhaseSettings& GetTrailPhaseSettings(
		const Engine::ParticleRenderSettings& settings, size_t phaseIndex) {

		static const Engine::ParticleTrailPhaseSettings kDefault{};
		if (phaseIndex < settings.trailPhaseSettings.size()) {
			return settings.trailPhaseSettings[phaseIndex];
		}
		return kDefault;
	}

	// トレイルのUV行列を生成する
	Engine::Matrix4x4 BuildTrailUVMatrix(const Engine::ParticleTrailUVAnimationSettings& settings,
		float lifetimeT, float age) {

		const Engine::Vector4 offsetValue = settings.scroll ?
			Engine::Vector4(settings.scrollSpeed.x * age, settings.scrollSpeed.y * age, 0.0f, 0.0f) :
			Engine::EvaluateParticleMaterialParameter(settings.offset, lifetimeT);
		const Engine::Vector4 scaleValue = Engine::EvaluateParticleMaterialParameter(settings.scale, lifetimeT);
		const float rotationValue = Engine::EvaluateParticleMaterialParameter(settings.rotation, lifetimeT).x;
		const Engine::Vector3 scale(scaleValue.x, scaleValue.y, 1.0f);
		const Engine::Vector3 rotation(0.0f, 0.0f, rotationValue);
		const Engine::Vector3 translation(offsetValue.x, offsetValue.y, 0.0f);
		const Engine::Matrix4x4 scaleRotation = Engine::Matrix4x4::MakeAffineMatrix(
			scale, rotation, Engine::Vector3::AnyInit(0.0f));
		const Engine::Vector3 pivot(settings.pivot.x, settings.pivot.y, 0.0f);
		const Engine::Vector3 translationWithPivot =
			translation + pivot - Engine::Vector3::Transform(pivot, scaleRotation);
		return Engine::Matrix4x4::MakeAffineMatrix(scale, rotation, translationWithPivot);
	}

	// 1点の幅と色を評価する
	EvaluatedTrailPoint EvaluatePoint(const Engine::ParticleTrailPoint& point, float ribbonT,
		const Engine::ParticleRenderSettings& settings) {

		EvaluatedTrailPoint result{};
		const Engine::ParticleTrailSettings& trail = settings.trail;
		result.lifetimeT = 0.0f < trail.pointLifetime ?
			(std::clamp)(point.age / trail.pointLifetime, 0.0f, 1.0f) : 1.0f - ribbonT;
		result.age = point.age;
		result.phaseIndex = point.phaseIndex;

		const Engine::ParticleTrailPhaseSettings& phase = GetTrailPhaseSettings(settings, point.phaseIndex);
		const Engine::Vector4 color = Engine::EvaluateParticleMaterialParameter(phase.color, result.lifetimeT);
		result.render.position = point.position;
		result.render.halfWidth = Engine::EvaluateParticleMaterialParameter(phase.width, result.lifetimeT).x * 0.5f;
		result.render.color = Engine::Color4(color.x, color.y, color.z, color.w);
		result.render.ribbonT = ribbonT;
		return result;
	}

	// 1セグメント分のマテリアルデータを追加する
	uint32_t AppendSegmentMaterial(const EvaluatedTrailPoint& point0, const EvaluatedTrailPoint& point1,
		const Engine::ParticleRenderSettings& settings, const Engine::ParticleCustomParameterLayout& customLayout,
		Engine::BlendMode blendMode, Engine::ParticleTrailRenderData& outData) {

		const Engine::ParticleTrailPhaseSettings& phase0 = GetTrailPhaseSettings(settings, point0.phaseIndex);
		const Engine::ParticleTrailPhaseSettings& phase1 = GetTrailPhaseSettings(settings, point1.phaseIndex);
		const float materialT = (point0.lifetimeT + point1.lifetimeT) * 0.5f;
		const float materialAge = (point0.age + point1.age) * 0.5f;
		const Engine::ParticleTrailPhaseSettings& materialPhase =
			point0.lifetimeT <= point1.lifetimeT ? phase0 : phase1;

		Engine::ParticleMaterialData material{};
		material.materialParams.z =
			static_cast<float>(blendMode);
		material.materialColor = Engine::Color4::White();
		material.uvMatrix = BuildTrailUVMatrix(materialPhase.uv, materialT, materialAge);
		const uint32_t materialIndex = static_cast<uint32_t>(outData.materials.size());
		outData.materials.emplace_back(material);

		if (customLayout.stride != 0) {

			std::vector<uint8_t> custom = customLayout.defaultData;
			custom.resize(customLayout.stride, 0);
			for (const Engine::ShaderConstantBufferVariable& variable : customLayout.variables) {

				auto parameter = materialPhase.parameters.find(variable.name);
				if (parameter == materialPhase.parameters.end()) {
					continue;
				}
				Engine::WriteParticleCustomParameter(custom, variable,
					Engine::EvaluateParticleMaterialParameter(parameter->second, materialT));
			}
			outData.customParameters.insert(outData.customParameters.end(), custom.begin(), custom.end());
		}
		return materialIndex;
	}
}

//============================================================================
//	ParticleTrailDataBuilder namespaceMethods
//============================================================================
void Engine::ParticleTrailDataBuilder::Build(const RenderDrawContext& context,
	std::span<const RenderItem* const> items, const ParticleCustomParameterLayout& customLayout,
	ParticleTrailRenderData& outData) {

	outData.Clear();
	std::vector<EvaluatedTrailPoint> trailPoints;
	for (const RenderItem* item : items) {

		const ParticleRenderPayload* payload = context.batch->GetPayload<ParticleRenderPayload>(*item);
		const ParticleGroupRuntimeState* group = payload ?
			ResolveParticleRenderGroup(*item, *payload) : nullptr;
		if (!group) {
			continue;
		}
		const ParticleRenderSettings& settings = group->renderSettings;

		for (const auto& trailPair : group->trails) {

			const ParticleTrailRuntime& runtime = trailPair.second;
			const std::deque<ParticleTrailPoint>& points = runtime.points;
			const size_t pointCount = points.size() + 1;
			if (pointCount < 2) {
				continue;
			}

			trailPoints.clear();
			trailPoints.reserve(pointCount);
			for (size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex) {

				const ParticleTrailPoint& point = pointIndex < points.size() ? points[pointIndex] : runtime.head;
				const float ribbonT = static_cast<float>(pointIndex) / static_cast<float>(pointCount - 1);
				EvaluatedTrailPoint evaluated = EvaluatePoint(point, ribbonT, settings);
				if (!trailPoints.empty()) {
					const Vector3 diff = evaluated.render.position - trailPoints.back().render.position;
					if (Vector3::Dot(diff, diff) <= 1e-8f) {
						trailPoints.back() = evaluated;
						continue;
					}
				}
				trailPoints.emplace_back(evaluated);
			}
			if (trailPoints.size() < 2) {
				continue;
			}

			const uint32_t pointOffset = static_cast<uint32_t>(outData.points.size());
			for (size_t pointIndex = 0; pointIndex < trailPoints.size(); ++pointIndex) {

				const Vector3& previous = trailPoints[pointIndex == 0 ? 0 : pointIndex - 1].render.position;
				const Vector3& next = trailPoints[(std::min)(pointIndex + 1, trailPoints.size() - 1)].render.position;
				trailPoints[pointIndex].render.tangent = Vector3::NormalizeOr(
					next - previous, Vector3(0.0f, 0.0f, 1.0f));
			}
			for (size_t segmentIndex = 0; segmentIndex + 1 < trailPoints.size(); ++segmentIndex) {

				trailPoints[segmentIndex].render.materialIndex = AppendSegmentMaterial(
					trailPoints[segmentIndex], trailPoints[segmentIndex + 1], settings,
					customLayout, item->blendMode, outData);
				outData.segments.emplace_back(pointOffset + static_cast<uint32_t>(segmentIndex));
			}
			trailPoints.back().render.materialIndex = trailPoints[trailPoints.size() - 2].render.materialIndex;
			for (const EvaluatedTrailPoint& point : trailPoints) {
				outData.points.emplace_back(point.render);
			}
		}
	}
}
