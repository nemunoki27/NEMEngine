#include "ParticleCircleEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	ParticleCircleEmitterShape internal
//============================================================================
namespace {

	constexpr float kEpsilon = 0.001f;

	// 円弧の情報、0度跨ぎと全周に対応する
	struct CircleArc {

		float base = 0.0f;   // 進行の基準端点
		float span = 360.0f; // 弧の長さ
		bool full = true;    // 全周か
		float sign = 1.0f;   // 進行方向の符号
	};
	CircleArc MakeArc(const Engine::ParticleEmitterCircleParams& circle) {

		CircleArc arc{};
		arc.full = 360.0f - kEpsilon <= std::fabs(circle.angleMax - circle.angleMin);
		const float angleMin = Math::WrapDegree360(circle.angleMin);
		const float angleMax = Math::WrapDegree360(circle.angleMax);
		arc.span = Engine::ParticleCircleEmitterShape::GetArcSpan(circle);
		arc.base = circle.clockwise ? angleMax : angleMin;
		arc.sign = circle.clockwise ? -1.0f : 1.0f;
		return arc;
	}

	// 発生インデックスから角度を取得する、度数法
	float GetSpawnAngle(const Engine::ParticleEmitterCircleParams& circle, const CircleArc& arc,
		uint32_t globalIndex, uint32_t batchIndex, uint32_t batchCount) {

		if (arc.span <= 0.0f) {
			return arc.base;
		}
		switch (circle.spawnMode) {
		case Engine::ParticleEmitterSpawnMode::EvenPerFrame: {

			// 1回の発生数で等間隔に並べる
			const float delta = arc.span / static_cast<float>((std::max)(batchCount, 1u));
			return Math::WrapDegree360(
				arc.base + (static_cast<float>(batchIndex) + 0.5f) * delta * arc.sign);
		}
		case Engine::ParticleEmitterSpawnMode::Progressive: {

			// 発生させるごとにステップ角度だけ進めて弧長で折り返す
			const float advance = std::fmod(
				std::fabs(circle.stepAngle) * static_cast<float>(globalIndex), arc.span);
			return Math::WrapDegree360(arc.base + advance * arc.sign);
		}
		case Engine::ParticleEmitterSpawnMode::Random:
		default: {

			// 範囲内でランダムに角度を決める
			const float degree = Engine::RandomGenerator::Generate(0.0f, arc.span);
			return Math::WrapDegree360(arc.base + degree * arc.sign);
		}
		}
	}

	// 円周の接線方向を取得する
	Engine::Vector3 GetTangent(float angleDegree, bool clockwise, bool wantNext, bool is2D) {

		float sign = clockwise ? -1.0f : 1.0f;
		if (!wantNext) {
			sign *= -1.0f;
		}
		const float radianAngle = angleDegree * Math::radian;
		return is2D ?
			Engine::Vector3(-std::sin(radianAngle) * sign, std::cos(radianAngle) * sign, 0.0f) :
			Engine::Vector3(-std::sin(radianAngle) * sign, 0.0f, std::cos(radianAngle) * sign);
	}

	// 速度の向きを取得する、隣接する発生点へ向けるモードに対応する
	Engine::Vector3 GetSpawnDirection(const Engine::ParticleEmitterCircleParams& circle, const CircleArc& arc,
		const Engine::ParticleSpawnIndex& spawnIndex, float angleDegree, const Engine::Vector3& position, bool is2D) {

		using SpawnMode = Engine::ParticleEmitterSpawnMode;
		using VelocityMode = Engine::ParticleEmitterVelocityMode;

		// Normalはそのまま法線方向を返す
		if (circle.velocityMode == VelocityMode::Normal) {
			return Engine::ParticleCircleEmitterShape::GetDirection(angleDegree * Math::radian, is2D);
		}
		// Randomは隣接点が無いので接線方向にする
		const bool wantNext = circle.velocityMode == VelocityMode::NextPoint;
		if (circle.spawnMode == SpawnMode::Random || arc.span <= kEpsilon) {
			return GetTangent(angleDegree, circle.clockwise, wantNext, is2D);
		}

		// 隣接する発生点のインデックスを取得する
		uint32_t neighborGlobal = spawnIndex.global;
		uint32_t neighborBatch = spawnIndex.batchIndex;
		if (circle.spawnMode == SpawnMode::EvenPerFrame) {
			if (arc.full) {

				// 全周なら発生位置をループさせる
				neighborBatch = wantNext ? (spawnIndex.batchIndex + 1) % spawnIndex.batchCount :
					(spawnIndex.batchIndex + spawnIndex.batchCount - 1) % spawnIndex.batchCount;
			} else {
				neighborBatch = wantNext ?
					(std::min)(spawnIndex.batchIndex + 1, spawnIndex.batchCount - 1) :
					(0 < spawnIndex.batchIndex ? spawnIndex.batchIndex - 1 : 0u);
			}
		} else {
			neighborGlobal = wantNext ? spawnIndex.global + 1 :
				(0 < spawnIndex.global ? spawnIndex.global - 1 : 0u);
		}

		// 角度を進めて最初に戻る瞬間は前の区間の向きを使う
		if (wantNext && circle.usePrevSegmentDirectionOnWrap) {

			bool wrapped = false;
			if (circle.spawnMode == SpawnMode::Progressive) {

				const float stepMag = std::fabs(circle.stepAngle);
				const float advance = std::fmod(stepMag * static_cast<float>(spawnIndex.global), arc.span);
				const float advanceNext = std::fmod(stepMag * static_cast<float>(spawnIndex.global + 1), arc.span);
				wrapped = advanceNext + kEpsilon < advance;
			} else if (arc.full) {
				wrapped = spawnIndex.batchCount <= spawnIndex.batchIndex + 1;
			}
			if (wrapped) {

				const uint32_t prevGlobal = 0 < spawnIndex.global ? spawnIndex.global - 1 : 0u;
				const uint32_t prevBatch = 0 < spawnIndex.batchIndex ? spawnIndex.batchIndex - 1 : 0u;
				const float prevAngle = GetSpawnAngle(circle, arc, prevGlobal, prevBatch, spawnIndex.batchCount);
				const Engine::Vector3 prevPosition =
					Engine::ParticleCircleEmitterShape::GetDirection(prevAngle * Math::radian, is2D) * circle.radius;
				const Engine::Vector3 diff = position - prevPosition;
				if (kEpsilon < Engine::Vector3::Length(diff)) {
					return Engine::Vector3::Normalize(diff);
				}
				return GetTangent(angleDegree, circle.clockwise, true, is2D);
			}
		}

		// 隣接点と発生位置の差分を向きにする、ほぼ同じ位置なら接線方向にする
		const float neighborAngle = GetSpawnAngle(circle, arc, neighborGlobal, neighborBatch, spawnIndex.batchCount);
		const Engine::Vector3 neighborPosition =
			Engine::ParticleCircleEmitterShape::GetDirection(neighborAngle * Math::radian, is2D) * circle.radius;
		const Engine::Vector3 diff = neighborPosition - position;
		if (Engine::Vector3::Length(diff) <= kEpsilon) {
			return GetTangent(angleDegree, circle.clockwise, wantNext, is2D);
		}
		return Engine::Vector3::Normalize(diff);
	}
}

//============================================================================
//	ParticleCircleEmitterShape classMethods
//============================================================================
void Engine::ParticleCircleEmitterShape::FromJson(const nlohmann::json& data, ParticleEmitterSettings& settings) const {

	ParticleEmitterCircleParams& circle = settings.circle;
	circle.radius = data.value("circleRadius", circle.radius);
	circle.angleMin = data.value("circleAngleMin", circle.angleMin);
	circle.angleMax = data.value("circleAngleMax", circle.angleMax);
	circle.clockwise = data.value("circleClockwise", circle.clockwise);
	circle.spawnMode = EnumAdapter<ParticleEmitterSpawnMode>::FromString(
		data.value("circleSpawnMode", "Random")).value_or(ParticleEmitterSpawnMode::Random);
	circle.stepAngle = data.value("circleStepAngle", circle.stepAngle);
	circle.velocityMode = EnumAdapter<ParticleEmitterVelocityMode>::FromString(
		data.value("circleVelocityMode", "Normal")).value_or(ParticleEmitterVelocityMode::Normal);
	circle.usePrevSegmentDirectionOnWrap =
		data.value("circleUsePrevSegmentDirectionOnWrap", circle.usePrevSegmentDirectionOnWrap);
}

void Engine::ParticleCircleEmitterShape::ToJson(nlohmann::json& data, const ParticleEmitterSettings& settings) const {

	const ParticleEmitterCircleParams& circle = settings.circle;
	data["circleRadius"] = circle.radius;
	data["circleAngleMin"] = circle.angleMin;
	data["circleAngleMax"] = circle.angleMax;
	data["circleClockwise"] = circle.clockwise;
	data["circleSpawnMode"] = EnumAdapter<ParticleEmitterSpawnMode>::ToString(circle.spawnMode);
	data["circleStepAngle"] = circle.stepAngle;
	data["circleVelocityMode"] = EnumAdapter<ParticleEmitterVelocityMode>::ToString(circle.velocityMode);
	data["circleUsePrevSegmentDirectionOnWrap"] = circle.usePrevSegmentDirectionOnWrap;
}

void Engine::ParticleCircleEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, bool is2D) const {

	// 連番情報なしはランダム発生と同じ扱いにする
	InitParticle(position, direction, settings, is2D, ParticleSpawnIndex{});
}

void Engine::ParticleCircleEmitterShape::InitParticle(Vector3& position, Vector3& direction,
	const ParticleEmitterSettings& settings, bool is2D, const ParticleSpawnIndex& spawnIndex) const {

	const ParticleEmitterCircleParams& circle = settings.circle;
	const CircleArc arc = MakeArc(circle);

	const float angle = GetSpawnAngle(circle, arc, spawnIndex.global, spawnIndex.batchIndex, spawnIndex.batchCount);
	position = Engine::ParticleCircleEmitterShape::GetDirection(angle * Math::radian, is2D) * circle.radius;
	direction = GetSpawnDirection(circle, arc, spawnIndex, angle, position, is2D);
}

float Engine::ParticleCircleEmitterShape::GetArcSpan(const ParticleEmitterCircleParams& circle) {

	// 一周以上の指定は全周として扱う
	if (360.0f - kEpsilon <= std::fabs(circle.angleMax - circle.angleMin)) {
		return 360.0f;
	}
	return Math::WrapDegree360(Math::WrapDegree360(circle.angleMax) - Math::WrapDegree360(circle.angleMin));
}

Engine::Vector3 Engine::ParticleCircleEmitterShape::GetDirection(float radianAngle, bool is2D) {

	// 2DはXY平面、3DはXZ平面へ展開する
	return is2D ? Vector3(std::cos(radianAngle), std::sin(radianAngle), 0.0f) :
		Vector3(std::cos(radianAngle), 0.0f, std::sin(radianAngle));
}
