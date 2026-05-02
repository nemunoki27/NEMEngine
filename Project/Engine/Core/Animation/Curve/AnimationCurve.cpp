#include "AnimationCurve.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	AnimationCurve classMethods
//============================================================================

namespace {

	float Clamp01(float value) {

		return (std::clamp)(value, 0.0f, 1.0f);
	}

	float Lerp(float a, float b, float t) {

		return a + (b - a) * t;
	}

	float Hermite(float p0, float p1, float m0, float m1, float t, float length) {

		const float t2 = t * t;
		const float t3 = t2 * t;

		const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
		const float h10 = t3 - 2.0f * t2 + t;
		const float h01 = -2.0f * t3 + 3.0f * t2;
		const float h11 = t3 - t2;
		return h00 * p0 + h10 * m0 * length + h01 * p1 + h11 * m1 * length;
	}

	float CalcAutoTangent(const std::vector<Engine::CurveKey>& keys, size_t index) {

		if (keys.size() <= 1) {
			return 0.0f;
		}

		const size_t prev = index == 0 ? index : index - 1;
		const size_t next = index + 1 < keys.size() ? index + 1 : index;
		if (prev == next) {
			return 0.0f;
		}

		const float timeLength = keys[next].time - keys[prev].time;
		if (std::abs(timeLength) <= 0.00001f) {
			return 0.0f;
		}
		return (keys[next].value - keys[prev].value) / timeLength;
	}

	void SetupChannel(Engine::CurveChannel& channel, const char* name, const Engine::Color4& color, float defaultValue) {

		channel.name = name;
		channel.displayColor = color;
		channel.defaultValue = defaultValue;
	}
}

float Engine::CurveChannel::Evaluate(float time) const {

	if (keys.empty()) {
		return defaultValue;
	}
	if (time <= keys.front().time) {
		return keys.front().value;
	}
	if (keys.back().time <= time) {
		return keys.back().value;
	}

	auto nextIt = std::upper_bound(keys.begin(), keys.end(), time,
		[](float t, const CurveKey& key) {
			return t < key.time;
		});

	const CurveKey& next = *nextIt;
	const CurveKey& prev = *(nextIt - 1);
	const float length = next.time - prev.time;
	if (length <= 0.00001f) {
		return prev.value;
	}

	const float t = Clamp01((time - prev.time) / length);
	switch (prev.interpolation) {
	case CurveInterpolationMode::Constant:
		return prev.value;
	case CurveInterpolationMode::Linear:
		return Lerp(prev.value, next.value, t);
	case CurveInterpolationMode::Cubic:
	{
		const size_t prevIndex = static_cast<size_t>((nextIt - 1) - keys.begin());
		const size_t nextIndex = static_cast<size_t>(nextIt - keys.begin());
		const float leaveTangent = CalcAutoTangent(keys, prevIndex);
		const float arriveTangent = CalcAutoTangent(keys, nextIndex);
		return Hermite(prev.value, next.value, leaveTangent, arriveTangent, t, length);
	}
	default:
		break;
	}
	return prev.value;
}

uint32_t Engine::CurveChannel::AddKey(float time, float value, CurveInterpolationMode interpolation) {

	CurveKey key{};
	key.time = time;
	key.value = value;
	key.interpolation = interpolation;
	keys.emplace_back(key);
	SortKeys();

	for (uint32_t i = 0; i < keys.size(); ++i) {
		if (keys[i].time == time && keys[i].value == value) {
			return i;
		}
	}
	return static_cast<uint32_t>(keys.size() - 1);
}

bool Engine::CurveChannel::RemoveKey(uint32_t index) {

	if (keys.size() <= index) {
		return false;
	}
	keys.erase(keys.begin() + index);
	return true;
}

void Engine::CurveChannel::SortKeys() {

	std::sort(keys.begin(), keys.end(),
		[](const CurveKey& lhs, const CurveKey& rhs) {
			return lhs.time < rhs.time;
		});
}

bool Engine::CurveChannel::GetTimeRange(float& outMinTime, float& outMaxTime) const {

	if (keys.empty()) {
		return false;
	}

	outMinTime = keys.front().time;
	outMaxTime = keys.front().time;
	for (const CurveKey& key : keys) {
		outMinTime = (std::min)(outMinTime, key.time);
		outMaxTime = (std::max)(outMaxTime, key.time);
	}
	return true;
}

bool Engine::CurveChannel::GetValueRange(float& outMinValue, float& outMaxValue) const {

	if (keys.empty()) {
		return false;
	}

	outMinValue = keys.front().value;
	outMaxValue = keys.front().value;
	for (const CurveKey& key : keys) {
		outMinValue = (std::min)(outMinValue, key.value);
		outMaxValue = (std::max)(outMaxValue, key.value);
	}
	return true;
}

Engine::CurveFloat::CurveFloat() {

	SetupChannel(channel, "Value", Color4::White(), 0.0f);
}

float Engine::CurveFloat::Evaluate(float time) const {

	return channel.Evaluate(time);
}

Engine::CurveVector3::CurveVector3() {

	SetupChannel(channels[0], "X", Color4::Red(), 0.0f);
	SetupChannel(channels[1], "Y", Color4(0.20f, 0.45f, 1.0f, 1.0f), 0.0f);
	SetupChannel(channels[2], "Z", Color4::Green(), 0.0f);
}

Engine::Vector3 Engine::CurveVector3::Evaluate(float time) const {

	return Vector3(channels[0].Evaluate(time), channels[1].Evaluate(time), channels[2].Evaluate(time));
}

Engine::CurveColor3::CurveColor3() {

	SetupChannel(channels[0], "R", Color4::Red(), 1.0f);
	SetupChannel(channels[1], "G", Color4::Green(), 1.0f);
	SetupChannel(channels[2], "B", Color4::Blue(), 1.0f);
}

Engine::Color3 Engine::CurveColor3::Evaluate(float time) const {

	return Color3(channels[0].Evaluate(time), channels[1].Evaluate(time), channels[2].Evaluate(time));
}

Engine::CurveColor4::CurveColor4() {

	SetupChannel(channels[0], "R", Color4::Red(), 1.0f);
	SetupChannel(channels[1], "G", Color4::Green(), 1.0f);
	SetupChannel(channels[2], "B", Color4::Blue(), 1.0f);
	SetupChannel(channels[3], "A", Color4(0.85f, 0.85f, 0.85f, 1.0f), 1.0f);
}

Engine::Color4 Engine::CurveColor4::Evaluate(float time) const {

	return Color4(
		channels[0].Evaluate(time),
		channels[1].Evaluate(time),
		channels[2].Evaluate(time),
		channels[3].Evaluate(time));
}

std::span<Engine::CurveChannel> Engine::GetCurveChannels(CurveFloat& curve) {

	return std::span<CurveChannel>(&curve.channel, 1);
}

std::span<Engine::CurveChannel> Engine::GetCurveChannels(CurveVector3& curve) {

	return curve.channels;
}

std::span<Engine::CurveChannel> Engine::GetCurveChannels(CurveColor3& curve) {

	return curve.channels;
}

std::span<Engine::CurveChannel> Engine::GetCurveChannels(CurveColor4& curve) {

	return curve.channels;
}

std::span<const Engine::CurveChannel> Engine::GetCurveChannels(const CurveFloat& curve) {

	return std::span<const CurveChannel>(&curve.channel, 1);
}

std::span<const Engine::CurveChannel> Engine::GetCurveChannels(const CurveVector3& curve) {

	return curve.channels;
}

std::span<const Engine::CurveChannel> Engine::GetCurveChannels(const CurveColor3& curve) {

	return curve.channels;
}

std::span<const Engine::CurveChannel> Engine::GetCurveChannels(const CurveColor4& curve) {

	return curve.channels;
}
