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
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>

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

	uint32_t AppendRibbonSegment(const Engine::ParticleMaterialData& material,
		const Engine::ParticleCustomParameterLayout& layout, Engine::ParticleTrailRenderData& outData) {

		const uint32_t index = static_cast<uint32_t>(outData.materials.size());
		outData.materials.emplace_back(material);
		outData.segments.emplace_back(static_cast<uint32_t>(outData.points.size()));
		outData.customParameters.insert(outData.customParameters.end(), layout.defaultData.begin(), layout.defaultData.end());
		return index;
	}

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
		material.velocityAge.w = (point0.source.age + point1.source.age) * 0.5f;
		material.lifetimeRandom = Engine::Vector4(settings.trail.pointLifetime, materialT, 0, 0);
		material.particleColor = Engine::Vector4((point0.source.color.r + point1.source.color.r) * 0.5f,
			(point0.source.color.g + point1.source.color.g) * 0.5f, (point0.source.color.b + point1.source.color.b) * 0.5f,
			(point0.source.color.a + point1.source.color.a) * 0.5f);
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
void Engine::ParticleTrailDataBuilder::Build(const TrailRendererRecord& trail, BlendMode blendMode,
	const ParticleCustomParameterLayout& customLayout, ParticleTrailRenderData& outData) {

	outData.Clear();
	const auto& settings = trail.settings;
	const auto& samples = trail.runtime.points;
	const auto center = [](const TrailSample& sample) { return (sample.root + sample.tip) * 0.5f; };
	const auto age = [&](const TrailSample& sample) {
		return (std::clamp)(static_cast<float>(trail.runtime.time - sample.time) / settings.lifetime, 0.0f, 1.0f);
	};
	if (settings.lifetime <= 0.0f) return;
	for (size_t begin = 0; begin < samples.size();) {

		size_t end = begin + 1;
		while (end < samples.size() && !samples[end].breakBefore) ++end;
		if (1 < end - begin) {

			const uint32_t offset = static_cast<uint32_t>(outData.points.size());
			float distance = 0.0f;
			for (size_t i = begin; i < end; ++i) {

				const auto& sample = samples[i];
				const auto& previous = samples[i == begin ? i : i - 1];
				const auto& next = samples[(std::min)(i + 1, end - 1)];
				const float t = age(sample);
				ParticleTrailPointData point{};
				point.position = center(sample);
				point.tangent = Vector3::NormalizeOr(center(next) - center(previous), Vector3(0, 1, 0));
				point.color = settings.colorGradient.Evaluate(t);
				float width = settings.width;
				if (settings.mode == TrailMode::TwoPoints) {

					const Vector3 across = sample.tip - sample.root;
					width = across.Length();
					point.side = Vector3::NormalizeOr(across, Vector3(1, 0, 0));
				} else if (settings.is2D) {
					point.side = Vector3::NormalizeOr(Vector3(point.tangent.y, -point.tangent.x, 0), Vector3(1, 0, 0));
				} else if (settings.alignment == TrailAlignment::TransformZ) {

					const Vector3 fallback = std::abs(point.tangent.y) < 0.9f ? Vector3(0, 1, 0) : Vector3(1, 0, 0);
					point.side = Vector3::NormalizeOr(Vector3::Cross(point.tangent, sample.normal),
						Vector3::NormalizeOr(Vector3::Cross(point.tangent, fallback), Vector3(1, 0, 0)));
				}
				point.halfWidth = 0.5f * width * settings.widthMultiplier * (std::max)(0.0f, settings.widthCurve.Evaluate(t));
				distance += ((sample.root - previous.root).Length() + (sample.tip - previous.tip).Length()) * 0.5f;
				point.ribbonT = distance;
				if (i + 1 < end) {

					const float midAge = (t + age(next)) * 0.5f;
					ParticleMaterialData material{};
					material.materialParams.z = static_cast<float>(blendMode);
					const float dt = static_cast<float>(next.time - sample.time);
					const auto velocity = 1e-6f < dt ? (center(next) - point.position) / dt : Vector3::AnyInit(0.0f);
					material.velocityAge = Vector4(velocity.x, velocity.y, velocity.z, midAge * settings.lifetime);
					material.lifetimeRandom = Vector4(settings.lifetime, midAge, 0, 0);
					const auto color = settings.colorGradient.Evaluate(midAge);
					material.particleColor = Vector4(color.r, color.g, color.b, color.a);
					point.materialIndex = AppendRibbonSegment(material, customLayout, outData);
				} else {
					point.materialIndex = outData.points.back().materialIndex;
				}
				outData.points.emplace_back(point);
			}
			if (settings.textureMode == TrailTextureMode::Stretch && 0.0f < distance) {
				for (size_t i = offset; i < outData.points.size(); ++i) outData.points[i].ribbonT /= distance;
			}
		}
		begin = end;
	}
}

void Engine::ParticleTrailDataBuilder::Build(const BeamRendererRecord& beam, BlendMode blendMode, const Matrix4x4& uvMatrix,
	const ParticleCustomParameterLayout& customLayout, ParticleTrailRenderData& outData) {

	outData.Clear();
	const auto& settings = beam.settings;
	const auto& samples = beam.runtime.points;
	for (size_t i = 0; i < samples.size(); ++i) {

		const BeamPoint& sample = samples[i];
		const bool hasPrevious = 0 < i && !sample.startStrip;
		const bool hasNext = i + 1 < samples.size() && !samples[i + 1].startStrip;
		if (!hasPrevious && !hasNext) continue;

		const auto& previous = hasPrevious ? samples[i - 1] : sample;
		const auto& next = hasNext ? samples[i + 1] : sample;
		ParticleTrailPointData point{};
		point.position = sample.position;
		point.tangent = Vector3::NormalizeOr(next.position - previous.position, Vector3(0, 1, 0));
		if (settings.is2D) {
			point.side = Vector3::NormalizeOr(Vector3(point.tangent.y, -point.tangent.x, 0), Vector3(1, 0, 0));
		}
		point.color = Color4::White();
		point.halfWidth = 0.5f * settings.width * settings.widthMultiplier * sample.widthScale *
			(settings.useWidthCurve ? (std::max)(0.0f, settings.widthCurve.Evaluate(sample.progress)) : 1.0f);
		point.ribbonT = sample.progress;
		if (hasNext) {
			const float t = (sample.progress + next.progress) * 0.5f;
			ParticleMaterialData material{};
			material.uvMatrix = uvMatrix;
			material.materialParams.z = static_cast<float>(blendMode);
			material.velocityAge.w = static_cast<float>(beam.runtime.time);
			material.lifetimeRandom = Vector4(settings.duration, t, 0, 0);
			material.particleColor = Vector4(1, 1, 1, 1);
			point.materialIndex = AppendRibbonSegment(material, customLayout, outData);
		} else {
			point.materialIndex = outData.points.back().materialIndex;
		}
		outData.points.emplace_back(point);
	}
}

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
