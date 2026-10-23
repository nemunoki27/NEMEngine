#include "ParticleTrailDataBuilder.h"

//============================================================================
//	include
//============================================================================
#include "ParticleRenderDataUtility.h"

#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Particle/ParticleSystemSettings.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	ParticleTrailDataBuilder internal
//============================================================================
namespace {

	struct EvaluatedTrailPoint {

		Engine::ParticleTrailPoint source;
		Engine::ParticleTrailPointData render{};
		float lifetimeT = 0.0f;
		Engine::Vector4 emissive = Engine::Vector4(0, 0, 0, 0);
	};

	// 1点の幅と色を評価する
	EvaluatedTrailPoint EvaluatePoint(const Engine::ParticleTrailPoint& point, float ribbonT,
		const Engine::ParticleRenderSettings& settings) {

		EvaluatedTrailPoint result{};
		result.source = point;
		const Engine::ParticleTrailSettings& trail = settings.trail;
		result.lifetimeT = 0.0f < trail.pointLifetime ?
			(std::clamp)(point.age / trail.pointLifetime, 0.0f, 1.0f) : 1.0f - ribbonT;

		result.emissive = point.emissive;
		result.render.position = point.position;
		result.render.halfWidth = std::lerp(trail.endWidth, trail.startWidth, ribbonT) * 0.5f;
		const auto color = Engine::Color4::Lerp(trail.endColor, trail.startColor, ribbonT);
		result.render.color = Engine::Color4(color.r * point.color.r, color.g * point.color.g,
			color.b * point.color.b, color.a * point.color.a * (1.0f - result.lifetimeT));
		result.render.ribbonT = ribbonT;
		return result;
	}

	// 1セグメント分のマテリアルデータを追加する
	uint32_t AppendSegmentMaterial(const EvaluatedTrailPoint& point0, const EvaluatedTrailPoint& point1,
		const Engine::ParticleRenderSettings& settings, const Engine::ParticleCustomParameterLayout& customLayout,
		Engine::BlendMode blendMode, Engine::ParticleTrailRenderData& outData) {

		const float materialT = (point0.lifetimeT + point1.lifetimeT) * 0.5f;
		Engine::ParticleMaterialData material{};
		material.emissive = (point0.emissive + point1.emissive) * 0.5f;
		material.materialParams.x = (point0.source.alphaReference + point1.source.alphaReference) * 0.5f;
		material.materialParams.z =
			static_cast<float>(blendMode);
		material.materialColor = Engine::Color4::White();
		const uint32_t materialIndex = static_cast<uint32_t>(outData.materials.size());
		outData.materials.emplace_back(material);

		if (customLayout.stride != 0) {

			const size_t offset = outData.customParameters.size();
			outData.customParameters.resize(offset + customLayout.stride, 0);
			auto custom = std::span(outData.customParameters).subspan(offset, customLayout.stride);
			std::copy_n(customLayout.defaultData.begin(), (std::min)(customLayout.defaultData.size(), custom.size()), custom.begin());
			for (const Engine::ShaderConstantBufferVariable& variable : customLayout.variables) {

				auto parameter = settings.trail.materialSettings.parameters.find(variable.name);
				if (parameter == settings.trail.materialSettings.parameters.end()) {
					continue;
				}
				Engine::WriteParticleCustomParameter(custom, variable,
					Engine::EvaluateParticleMaterialParameter(parameter->second, materialT));
			}
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
		const ParticleEmitterRuntime* group = payload ?
			ResolveParticleRenderEmitter(*item, *payload) : nullptr;
		if (!group) {
			continue;
		}
		const ParticleRenderSettings& settings = group->renderSettings;

		for (const auto handle : group->trails.GetHandles()) {

			const ParticleTrailRuntime& runtime = *group->trails.TryGet(handle);
			const size_t pointCount = runtime.count + 1;
			if (pointCount < 2) {
				continue;
			}

			trailPoints.clear();
			trailPoints.reserve(pointCount);
			for (size_t pointIndex = 0; pointIndex < pointCount; ++pointIndex) {

				const ParticleTrailPoint& point = pointIndex < runtime.count ? group->trails.GetPoint(runtime, static_cast<uint32_t>(pointIndex)) : runtime.head;
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
			float distance = 0.0f;
			for (size_t pointIndex = 0; pointIndex < trailPoints.size(); ++pointIndex) {

				// 重複点を除いた後もUVの両端を0と1に合わせる
				float ribbonT = static_cast<float>(pointIndex) / static_cast<float>(trailPoints.size() - 1);
				EvaluatedTrailPoint& point = trailPoints[pointIndex];
				point = EvaluatePoint(point.source, ribbonT, settings);
				if (pointIndex > 0) {
					const auto delta = point.render.position - trailPoints[pointIndex - 1].render.position;
					distance += std::sqrt(Vector3::Dot(delta, delta));
				}
				if (settings.trail.textureMode == ParticleTrailTextureMode::Tile)
					point.render.ribbonT = distance / settings.trail.tileLength;
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
