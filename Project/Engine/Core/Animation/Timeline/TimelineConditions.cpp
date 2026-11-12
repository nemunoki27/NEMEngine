#include "TimelinePlaybackState.h"

//============================================================================
//	include
//============================================================================
#include "TimelinePlayerAccess.h"
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

// c++
#include <algorithm>
#include <cmath>

using namespace Engine;

namespace {

	struct ConditionSampling {

		bool active = false;
	};

	// 条件で使える数値を同じ精度へそろえる
	bool Number(const TimelineParameterValue& value, double& out) {

		return std::visit([&](const auto& typed) {
			using T = std::decay_t<decltype(typed)>;
			if constexpr (std::is_arithmetic_v<T>) { out = static_cast<double>(typed); return true; }
			else return false;
		}, value);
	}

	// 比較演算を分岐と距離条件で共用する
	bool Compare(double first, double second, TimelineComparison comparison) {

		switch (comparison) {
		case TimelineComparison::Equal: return std::abs(first - second) <= 1e-6;
		case TimelineComparison::NotEqual: return std::abs(first - second) > 1e-6;
		case TimelineComparison::Less: return first < second;
		case TimelineComparison::LessEqual: return first <= second;
		case TimelineComparison::Greater: return first > second;
		case TimelineComparison::GreaterEqual: return first >= second;
		}
		return false;
	}

	// 永続識別子から現在の参照先を取得する
	Entity FindTarget(const TimelinePlaybackState& state, Engine::UUID id) {

		for (const auto& binding : state.bindings) if (binding.id == id) return binding.entity;
		return Entity::Null();
	}

	// 未設定の対象は距離をゼロとみなさず条件不成立にする
	bool TestCondition(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
		TimelinePlaybackState& state, const TimelineCondition& condition, double time) {

		if (condition.kind == TimelineConditionKind::Parameter) {
			const auto* parameter = GetTimelineParameterValue(*state.asset, player.settings, condition.parameter);
			if (!parameter) return false;
			double first = 0.0, second = 0.0;
			if (Number(*parameter, first) && Number(condition.value, second)) return Compare(first, second, condition.comparison);
			const auto* text = std::get_if<std::string>(parameter);
			const auto* expected = std::get_if<std::string>(&condition.value);
			if (text && expected) return condition.comparison == TimelineComparison::Equal ? *text == *expected :
				condition.comparison == TimelineComparison::NotEqual && *text != *expected;
			return false;
		}
		const auto first = FindTarget(state, condition.firstBinding);
		const bool alive = world.IsAlive(first) && !world.IsPendingDestroy(first);
		if (condition.kind == TimelineConditionKind::BindingExists) {
			const bool expected = std::get_if<bool>(&condition.value) ? std::get<bool>(condition.value) : true;
			return condition.comparison == TimelineComparison::NotEqual ? alive != expected : alive == expected;
		}
		const auto second = FindTarget(state, condition.secondBinding);
		ResolvedWorldTransform firstPose, secondPose;
		if (!alive || !world.IsAlive(second) || world.IsPendingDestroy(second)) return false;
		auto& sampling = world.GetStorage().Get<ConditionSampling>();
		bool sampled = false;
		if (state.isScrubbing && !sampling.active) {
			sampling.active = true;
			ScopedCleanup restore([&]() noexcept { sampling.active = false; });
			sampled = SampleTimelineTargetTransform(world, owner, player, state, state.lastContext, time, first, firstPose.matrix) &&
				SampleTimelineTargetTransform(world, owner, player, state, state.lastContext, time, second, secondPose.matrix);
		}
		if (!sampled && (!TransformWorldUtility::ResolveWorldTransform(world, first, firstPose, true) ||
			!TransformWorldUtility::ResolveWorldTransform(world, second, secondPose, true))) return false;
		auto offset = secondPose.matrix.GetTranslationValue() - firstPose.matrix.GetTranslationValue();
		if (condition.plane == TimelineDistancePlane::GroundXZ) offset.y = 0.0f;
		if (condition.plane == TimelineDistancePlane::PlaneXY) offset.z = 0.0f;
		return Compare(offset.Length(), condition.distance, condition.comparison);
	}

	// グループ開始時の条件を一度だけ確定する
	bool TestConditions(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
		TimelinePlaybackState& state, const TimelineTrack& track, double time) {

		if (track.conditions.empty()) return true;
		for (const auto& condition : track.conditions) {
			const bool value = TestCondition(world, owner, player, state, condition,
				track.kind == TimelineTrackKind::Group ? track.start : time);
			if (track.conditionsAll && !value) return false;
			if (!track.conditionsAll && value) return true;
		}
		return track.conditionsAll;
	}
}

bool Engine::IsTimelineTrackActive(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
	TimelinePlaybackState& state, const TimelineTrack& track, double time) {

	if (track.muted) return false;
	if (track.parent) {
		const auto parent = std::find_if(state.asset->tracks.begin(), state.asset->tracks.end(),
			[&](const auto& candidate) { return candidate.id == track.parent; });
		if (parent == state.asset->tracks.end() || !IsTimelineTrackActive(world, owner, player, state, *parent, time)) return false;
	}
	const double end = track.duration > 0.0 ? track.start + track.duration : state.asset->duration;
	if (track.kind == TimelineTrackKind::Group && (time < track.start || time > end)) return false;
	if (track.branchGroup) {
		auto branch = state.branches.find(track.branchGroup);
		if (branch == state.branches.end()) {
			Engine::UUID selected{}, fallback{};
			for (const auto& candidate : state.asset->tracks) {
				if (candidate.branchGroup != track.branchGroup || candidate.parent != track.parent || candidate.muted) continue;
				if (candidate.fallback) { if (!fallback) fallback = candidate.id; continue; }
				if (TestConditions(world, owner, player, state, candidate, time)) { selected = candidate.id; break; }
			}
			branch = state.branches.emplace(track.branchGroup, selected ? selected : fallback).first;
		}
		return branch->second == track.id;
	}
	const auto found = state.groups.find(track.id);
	if (found != state.groups.end()) return found->second;
	return state.groups.emplace(track.id, TestConditions(world, owner, player, state, track, time)).first->second;
}
