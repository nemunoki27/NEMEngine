#include "MotionAnimationEvaluation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Engine;

namespace {

	// 移動平面に含まれない軸を開始位置に固定する
	Vector3 PlanePosition(Vector3 value, Vector3 origin, MotionAnimationPlane plane) {

		if (plane == MotionAnimationPlane::GroundXZ) value.y = origin.y;
		if (plane == MotionAnimationPlane::PlaneXY) value.z = origin.z;
		return value;
	}

	// 開始姿勢を基準に距離をワールド座標へ変換する
	Vector3 RelativePoint(const MotionAnimationRuntime& runtime, Vector3 offset, MotionAnimationPlane plane) {

		return PlanePosition(runtime.origin + Vector3::Transform(offset, Quaternion::MakeRotateMatrix(runtime.originRotation)),
			runtime.origin, plane);
	}

	// 遅延と繰り返し回数を反映した評価時刻を求める
	double EffectiveTime(const MotionAnimationSettings& settings, double time) {

		const double duration = GetMotionAnimationDuration(settings);
		const double cycle = duration * (settings.repeat == SimpleAnimationRepeat::PingPong ? 2 : 1);
		const double limit = settings.repeat == SimpleAnimationRepeat::Once ? duration :
			(settings.cycleCount ? cycle * settings.cycleCount : 1e12);
		return std::clamp(time - settings.delay, 0.0, limit);
	}

	// 周期境界の通過回数をまとめて数える
	uint64_t Crossings(double time, double first, double period) {

		return time < first ? 0 : static_cast<uint64_t>(std::floor((time - first) / period)) + 1;
	}
}

void Engine::SetMotionAnimationRotationEdit(MotionAnimationSettings& settings, SimpleAnimationRotationMode mode) {

	if (settings.rotationEdit == mode) return;
	if (mode == SimpleAnimationRotationMode::AxisAngle) {
		const auto rotation = Quaternion::FromEulerDegrees(settings.euler).Normalize();
		const Vector3 vector(rotation.x, rotation.y, rotation.z);
		settings.axis = Vector3::NormalizeOr(vector, {0, 1, 0});
		settings.angle = 2.0f * std::acos(std::clamp(rotation.w, -1.0f, 1.0f)) * 180.0f / std::numbers::pi_v<float>;
	} else {
		const auto rotation = Quaternion::MakeAxisAngle(Vector3::NormalizeOr(settings.axis, {0, 1, 0}),
			settings.angle * std::numbers::pi_v<float> / 180.0f);
		settings.euler = Quaternion::ToEulerDegrees(rotation);
	}
	settings.rotationEdit = mode;
}

double Engine::GetMotionAnimationDuration(const MotionAnimationSettings& settings) {

	if (settings.mode != MotionAnimationMode::Segments) return (std::max)(0.0001, static_cast<double>(settings.duration));
	double result = 0;
	for (const auto& segment : settings.segments) result += (std::max)(0.0001, static_cast<double>(segment.duration));
	return (std::max)(0.0001, result);
}

MotionAnimationSample Engine::EvaluateMotionAnimation(const MotionAnimationSettings& settings,
	const MotionAnimationRuntime& runtime, double time) {

	MotionAnimationSample result;
	result.position = runtime.origin;
	result.rotation = runtime.originRotation;
	if (time < settings.delay) return result;
	const double duration = GetMotionAnimationDuration(settings);
	const double cycle = duration * (settings.repeat == SimpleAnimationRepeat::PingPong ? 2 : 1);
	const double elapsed = EffectiveTime(settings, time);
	result.cycles = static_cast<uint64_t>(std::floor(elapsed / cycle));
	result.finished = time >= settings.delay && (settings.repeat == SimpleAnimationRepeat::Once ? elapsed >= duration :
		settings.cycleCount > 0 && elapsed >= cycle * settings.cycleCount);
	double phase = std::fmod(elapsed, cycle);
	if (result.finished) phase = settings.repeat == SimpleAnimationRepeat::PingPong ? 0 : duration;
	const bool reverse = phase > duration;
	if (reverse) phase = cycle - phase;
	const float progress = static_cast<float>(phase / duration);
	const float eased = EasedValue(settings.easing, progress);
	Vector3 end = RelativePoint(runtime, settings.offset, settings.plane);
	if (settings.mode == MotionAnimationMode::ApproachTarget) {
		const auto toward = PlanePosition(runtime.targetPosition, runtime.origin, settings.plane) - runtime.origin;
		const float distance = toward.Length();
		end = runtime.origin + Vector3::NormalizeOr(toward, {}) * (std::max)(0.0f, distance - settings.stoppingDistance);
	}
	result.position = Vector3::Lerp(runtime.origin, end, eased);
	result.tangent = end - runtime.origin;
	if (settings.mode == MotionAnimationMode::Segments && !settings.segments.empty()) {
		Vector3 start = runtime.origin;
		double remaining = phase;
		for (size_t i = 0; i < settings.segments.size(); ++i) {
			const auto& segment = settings.segments[i];
			end = RelativePoint(runtime, segment.offset, settings.plane);
			if (remaining <= segment.duration || i + 1 == settings.segments.size()) {
				const float t = static_cast<float>(std::clamp(remaining / segment.duration, 0.0, 1.0));
				result.position = Vector3::Lerp(start, end, EasedValue(segment.easing, t));
				result.tangent = end - start;
				break;
			}
			remaining -= segment.duration;
			start = end;
		}
	}
	if (settings.mode == MotionAnimationMode::Spline && !runtime.splineSamples.empty()) {
		const auto sample = EvaluateSplineDistance(runtime.splineSamples,
			settings.splineStart + (settings.splineEnd - settings.splineStart) * eased, false);
		result.position = PlanePosition(sample.position, runtime.origin, settings.plane);
		result.tangent = sample.tangent * (settings.splineEnd < settings.splineStart ? -1.0f : 1.0f);
	}
	if (reverse) result.tangent = result.tangent * -1.0f;
	result.tangent = PlanePosition(runtime.origin + result.tangent, runtime.origin, settings.plane) - runtime.origin;
	if (settings.rotation == MotionAnimationRotation::Relative) {
		const auto rotation = settings.rotationEdit == SimpleAnimationRotationMode::Euler ? Quaternion::FromEulerDegrees(settings.euler * eased) :
			Quaternion::MakeAxisAngle(Vector3::NormalizeOr(settings.axis, {0, 1, 0}), settings.angle * eased * std::numbers::pi_v<float> / 180.0f);
		result.rotation = (runtime.originRotation * rotation).Normalize();
	} else {
		Vector3 direction = settings.rotation == MotionAnimationRotation::LookAtTarget ?
			runtime.targetPosition - result.position : result.tangent;
		direction = PlanePosition(runtime.origin + direction, runtime.origin, settings.plane) - runtime.origin;
		if (direction.Length() > 0.00001f) {
			if (settings.plane == MotionAnimationPlane::PlaneXY)
				result.rotation = Quaternion::MakeAxisAngle({0, 0, 1}, std::atan2(direction.y, direction.x));
			else result.rotation = Quaternion::LookRotation(direction.Normalize(), {0, 1, 0});
			if (settings.rotation == MotionAnimationRotation::LookAtTarget)
				result.rotation = Quaternion::Lerp(runtime.originRotation, result.rotation, eased).Normalize();
		}
	}
	return result;
}

void Engine::AccumulateMotionAnimationEvents(const MotionAnimationSettings& settings, MotionAnimationRuntime& runtime,
	double previousTime, double currentTime) {

	if (currentTime <= previousTime) return;
	const double duration = GetMotionAnimationDuration(settings);
	const double cycle = duration * (settings.repeat == SimpleAnimationRepeat::PingPong ? 2 : 1);
	const double from = EffectiveTime(settings, previousTime), to = EffectiveTime(settings, currentTime);
	const size_t count = settings.mode == MotionAnimationMode::Segments ? settings.segments.size() : 1;
	runtime.pendingSegments.resize(count);
	double end = 0;
	for (size_t i = 0; i < count; ++i) {
		end += settings.mode == MotionAnimationMode::Segments ? settings.segments[i].duration : duration;
		uint64_t amount = Crossings(to, end, cycle) - Crossings(from, end, cycle);
		if (settings.repeat == SimpleAnimationRepeat::PingPong) {
			const double start = end - (settings.mode == MotionAnimationMode::Segments ? settings.segments[i].duration : duration);
			amount += Crossings(to, cycle - start, cycle) - Crossings(from, cycle - start, cycle);
		}
		runtime.pendingSegments[i] += amount;
	}
	runtime.pendingCycles += static_cast<uint64_t>(std::floor(to / cycle)) - static_cast<uint64_t>(std::floor(from / cycle));
	const auto before = EvaluateMotionAnimation(settings, runtime, previousTime);
	const auto after = EvaluateMotionAnimation(settings, runtime, currentTime);
	if (!before.finished && after.finished) ++runtime.pendingCompletions;
}

std::vector<Vector3> Engine::GetMotionAnimationDebugPoints(ECSWorld& world, Entity entity) {

	const auto* record = TryGetMotionAnimation(world, entity);
	if (!record || !record->settings.drawDebug) return {};
	auto runtime = record->runtime;
	if (!runtime.initialized) {
		ResolvedWorldTransform pose;
		if (!TransformWorldUtility::ResolveWorldTransform(world, entity, pose)) return {};
		runtime.origin = pose.matrix.GetTranslationValue();
		runtime.originRotation = pose.rotation;
		ResolvedWorldTransform target;
		if (TransformWorldUtility::ResolveWorldTransform(world, record->settings.target.Resolve(world, entity), target))
			runtime.targetPosition = target.matrix.GetTranslationValue();
		const Entity splineEntity = runtime.hasSplineOverride ? runtime.splineOverride : record->settings.spline.Resolve(world, entity);
		const auto* spline = TryGetSpline(world, splineEntity);
		if (spline && TransformWorldUtility::ResolveWorldTransform(world, splineEntity, target))
			runtime.splineSamples = BuildSplineSamples(*spline, target.matrix);
	}
	auto settings = record->settings;
	settings.repeat = SimpleAnimationRepeat::Once;
	std::vector<Vector3> points;
	for (int i = 0; i <= 64; ++i) points.push_back(EvaluateMotionAnimation(settings, runtime,
		settings.delay + GetMotionAnimationDuration(settings) * i / 64.0).position);
	return points;
}
