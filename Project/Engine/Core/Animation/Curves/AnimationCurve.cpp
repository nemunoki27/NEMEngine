#include "AnimationCurve.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/Axis.h>
#include <Engine/Core/Foundation/Math/Math.h>

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	AnimationCurve classMethods
//============================================================================
namespace {

	// 前後の値と接線でHermite補間する
	float Hermite(float p0, float p1, float m0, float m1, float t, float length) {

		// 時間あたりの傾きから補間係数を作る
		const float t2 = t * t;
		const float t3 = t2 * t;

		const float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
		const float h10 = t3 - 2.0f * t2 + t;
		const float h01 = -2.0f * t3 + 3.0f * t2;
		const float h11 = t3 - t2;
		return h00 * p0 + h10 * m0 * length + h01 * p1 + h11 * m1 * length;
	}

	// 四つの制御点でBezier補間する
	float CubicBezier(float p0, float p1, float p2, float p3, float t) {

		const float u = 1.0f - t;
		return u * u * u * p0 +
			3.0f * u * u * t * p1 +
			3.0f * u * t * t * p2 +
			t * t * t * p3;
	}

	// 時刻に対応するBezier曲線の値を求める
	float EvaluateBezierSegment(const Engine::CurveKey& prev, const Engine::CurveKey& next, float time, float normalizedT) {

		// 時刻からBezierの位置を逆算する
		const float p0x = prev.time;
		const float p1x = prev.time + prev.outTangent.x;
		const float p2x = next.time + next.inTangent.x;
		const float p3x = next.time;

		// 接線が未指定なら直線で補間する
		if (std::abs(prev.outTangent.x) <= 0.00001f && std::abs(next.inTangent.x) <= 0.00001f) {
			return Math::Lerp(prev.value, next.value, normalizedT);
		}

		float low = 0.0f;
		float high = 1.0f;
		float bezierT = normalizedT;
		for (uint32_t i = 0; i < 16; ++i) {

			// 二分探索で指定時刻に近い位置を探す
			bezierT = (low + high) * 0.5f;
			const float x = CubicBezier(p0x, p1x, p2x, p3x, bezierT);
			if (x < time) {
				low = bezierT;
			} else {
				high = bezierT;
			}
		}

		const float p0y = prev.value;
		const float p1y = prev.value + prev.outTangent.y;
		const float p2y = next.value + next.inTangent.y;
		const float p3y = next.value;
		return CubicBezier(p0y, p1y, p2y, p3y, bezierT);
	}

	// 前後のキーから自動接線を求める
	float CalcAutoTangent(const std::vector<Engine::CurveKey>& keys, size_t index) {

		// 端のキーは片側の傾きを使う
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

	// チャンネルの表示と既定値を設定する
	void SetupChannel(Engine::CurveChannel& channel, const char* name, const Engine::Color4& color, float defaultValue) {

		channel.name = name;
		channel.displayColor = color;
		channel.defaultValue = defaultValue;
	}

	// 現在時刻に対応する軸キーの番号を求める
	uint32_t FindAxisKeyIndex(const Engine::CurveChannel& axisChannel, float time) {

		// 先頭と末尾では端のキーを使う
		if (axisChannel.keys.empty()) {
			return 0;
		}
		if (time <= axisChannel.keys.front().time) {
			return 0;
		}
		if (axisChannel.keys.back().time <= time) {
			return static_cast<uint32_t>(axisChannel.keys.size() - 1);
		}

		auto nextIt = std::upper_bound(axisChannel.keys.begin(), axisChannel.keys.end(), time,
			[](float t, const Engine::CurveKey& key) {
				return t < key.time;
			});
		return static_cast<uint32_t>((nextIt - 1) - axisChannel.keys.begin());
	}
}

float Engine::CurveChannel::Evaluate(float time) const {

	// キーがなければチャンネルの既定値を返す
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

	const float t = Math::Saturate((time - prev.time) / length);
	switch (prev.interpolation) {
	case CurveInterpolationMode::Constant:
		// 次キー直前まで値を保持する
		return prev.value;
	case CurveInterpolationMode::Linear:
		return Math::Lerp(prev.value, next.value, t);
	case CurveInterpolationMode::Bezier:
		return EvaluateBezierSegment(prev, next, time, t);
	case CurveInterpolationMode::Spline:
	case CurveInterpolationMode::Squad:
	{
		// 数値カーブのSquad指定はSplineで評価する
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

	// キーを追加して時間順へ並べる
	CurveKey key{};
	key.time = time;
	key.value = value;
	key.interpolation = interpolation;
	keys.emplace_back(key);
	SortKeys();

	// 追加した値と時刻に一致するキーの番号を返す
	for (uint32_t i = 0; i < keys.size(); ++i) {
		if (keys[i].time == time && keys[i].value == value) {
			return i;
		}
	}
	return static_cast<uint32_t>(keys.size() - 1);
}

bool Engine::CurveChannel::RemoveKey(uint32_t index) {

	// 存在するキーだけを削除する
	if (keys.size() <= index) {
		return false;
	}
	keys.erase(keys.begin() + index);
	return true;
}

void Engine::CurveChannel::SortKeys() {

	// 補間で参照する時間順へ揃える
	std::sort(keys.begin(), keys.end(),
		[](const CurveKey& lhs, const CurveKey& rhs) {
			return lhs.time < rhs.time;
		});
}

bool Engine::CurveChannel::GetTimeRange(float& outMinTime, float& outMaxTime) const {

	if (keys.empty()) {
		return false;
	}

	// 全キーの時間範囲を求める
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

	// 全キーの値の範囲を求める
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

Engine::CurveQuaternion::CurveQuaternion() {

	SetupChannel(channels[0], "Axis", Color4(0.95f, 0.85f, 0.20f, 1.0f), 0.0f);
	SetupChannel(channels[1], "Angle", Color4(0.25f, 0.65f, 1.0f, 1.0f), 0.0f);
}

Engine::Quaternion Engine::CurveQuaternion::Evaluate(float time) const {

	// 回転軸と角度からQuaternionへ変換する
	const Vector3 axis = EvaluateAxis(time);
	const float angleDegrees = EvaluateAngle(time);
	return Quaternion::Normalize(Quaternion::MakeAxisAngle(axis, Math::DegToRad(angleDegrees)));
}

Engine::Vector3 Engine::CurveQuaternion::EvaluateAxis(float time) const {

	// 対応する軸設定がなければ既定の軸を使う
	const uint32_t axisKeyIndex = FindAxisKeyIndex(channels[0], time);
	const CurveQuaternionAxisKey fallbackAxisKey = QuaternionAxisKeyUtility::MakeDefault();
	const CurveQuaternionAxisKey& axisKey = axisKeyIndex < axisKeys.size() ? axisKeys[axisKeyIndex] : fallbackAxisKey;
	return QuaternionAxisKeyUtility::GetAxisDirection(axisKey);
}

float Engine::CurveQuaternion::EvaluateAngle(float time) const {

	return channels[1].Evaluate(time);
}

void Engine::CurveQuaternion::EnsureAxisKeyCount() {

	// 軸設定の数をキー数へ揃える
	if (channels[0].keys.empty()) {
		axisKeys.clear();
		return;
	}
	while (axisKeys.size() < channels[0].keys.size()) {
		axisKeys.emplace_back(QuaternionAxisKeyUtility::MakeDefault());
	}
	if (channels[0].keys.size() < axisKeys.size()) {
		axisKeys.resize(channels[0].keys.size());
	}
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

std::span<Engine::CurveChannel> Engine::GetCurveChannels(CurveQuaternion& curve) {

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

std::span<const Engine::CurveChannel> Engine::GetCurveChannels(const CurveQuaternion& curve) {

	return curve.channels;
}
