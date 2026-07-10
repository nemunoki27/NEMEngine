#include "ParticleCircleEmitterShape.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Particle/Gui/ParticleGuiHelpers.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
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

	// 角度からローカルの半径方向を取得する、3DはXZ平面で2DはXY平面
	Engine::Vector3 AngleToDirection(float radianAngle, bool is2D) {

		return is2D ?
			Engine::Vector3(std::cos(radianAngle), std::sin(radianAngle), 0.0f) :
			Engine::Vector3(std::cos(radianAngle), 0.0f, std::sin(radianAngle));
	}

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
		arc.span = arc.full ? 360.0f : Math::WrapDegree360(angleMax - angleMin);
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
			return AngleToDirection(angleDegree * Math::radian, is2D);
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
					AngleToDirection(prevAngle * Math::radian, is2D) * circle.radius;
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
			AngleToDirection(neighborAngle * Math::radian, is2D) * circle.radius;
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
	// 旧スキーマの円弧角度は最大角へ引き継ぐ
	if (!data.contains("circleAngleMax") && data.contains("circleArc")) {

		circle.angleMin = 0.0f;
		circle.angleMax = data.value("circleArc", 360.0f);
	}
	circle.clockwise = data.value("circleClockwise", circle.clockwise);
	circle.spawnMode = EnumAdapter<ParticleEmitterSpawnMode>::FromString(
		data.value("circleSpawnMode", "Random")).value_or(ParticleEmitterSpawnMode::Random);
	circle.stepAngle = data.value("circleStepAngle", circle.stepAngle);
	// 旧スキーマの順番発生はステップ角度へ換算して引き継ぐ
	if (!data.contains("circleSpawnMode") && data.value("circleOrder", "Random") == "Sequential") {

		circle.spawnMode = ParticleEmitterSpawnMode::Progressive;
		const int32_t orderDivide = (std::max)(data.value("circleOrderDivide", 16), 1);
		circle.stepAngle = (circle.angleMax - circle.angleMin) / static_cast<float>(orderDivide);
	}
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
	position = AngleToDirection(angle * Math::radian, is2D) * circle.radius;
	direction = GetSpawnDirection(circle, arc, spawnIndex, angle, position, is2D);
}

void Engine::ParticleCircleEmitterShape::DrawShape(const ParticleEmitterSettings& settings,
	const Vector3& center, const Quaternion& rotation, bool is2D) const {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

	constexpr uint32_t kDivision = 24;

	const ParticleEmitterCircleParams& circle = settings.circle;
	const CircleArc arc = MakeArc(circle);
	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
	const Color4 color = Color4::Red();

	// 弧の範囲だけ線を張る
	const float startAngle = Math::WrapDegree360(circle.angleMin) * Math::radian;
	const float step = arc.span * Math::radian / static_cast<float>(kDivision);

	// 2Dはスクリーン空間の2Dレンダラーで描く
	if (is2D) {

		LineRenderer2D* renderer2D = LineRenderer::GetInstance()->Get2D();
		if (!renderer2D) {
			return;
		}
		// ローカル点をエンティティの回転と位置でスクリーン座標へ変換する
		auto toScreen = [&](const Vector3& local) {
			const Vector3 world = center + Vector3::Transform(local, rotationMatrix);
			return Vector2(world.x, world.y);
			};
		for (uint32_t i = 0; i < kDivision; ++i) {

			const float angle0 = startAngle + step * static_cast<float>(i);
			const float angle1 = startAngle + step * static_cast<float>(i + 1);
			renderer2D->DrawLine(
				toScreen(AngleToDirection(angle0, true) * circle.radius),
				toScreen(AngleToDirection(angle1, true) * circle.radius), color);
		}
		return;
	}

	LineRenderer3D* renderer = LineRenderer::GetInstance()->Get3D();
	if (!renderer) {
		return;
	}
	for (uint32_t i = 0; i < kDivision; ++i) {

		const float angle0 = startAngle + step * static_cast<float>(i);
		const float angle1 = startAngle + step * static_cast<float>(i + 1);
		renderer->DrawLine(
			center + Vector3::Transform(AngleToDirection(angle0, false) * circle.radius, rotationMatrix),
			center + Vector3::Transform(AngleToDirection(angle1, false) * circle.radius, rotationMatrix), color);
	}
#endif
}

bool Engine::ParticleCircleEmitterShape::DrawImGui(ParticleEmitterSettings& settings) const {

	ParticleEmitterCircleParams& circle = settings.circle;
	bool changed = false;
	changed |= MyGUI::DragFloat("半径", circle.radius, ParticleGui::MakeDragSetting(0.0f, 10000.0f)).valueChanged;
	changed |= MyGUI::DragFloat("最小角", circle.angleMin, ParticleGui::MakeDragSetting(-360.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::DragFloat("最大角", circle.angleMax, ParticleGui::MakeDragSetting(-360.0f, 360.0f, 0.5f)).valueChanged;
	changed |= MyGUI::Checkbox("時計回り", circle.clockwise);

	changed |= MyGUI::EnumCombo("発生方法", circle.spawnMode).valueChanged;
	if (circle.spawnMode == ParticleEmitterSpawnMode::Progressive) {
		changed |= MyGUI::DragFloat("ステップ角度", circle.stepAngle, ParticleGui::MakeDragSetting(0.0f, 360.0f, 0.5f)).valueChanged;
	}

	changed |= MyGUI::EnumCombo("速度の向き", circle.velocityMode).valueChanged;
	if (circle.velocityMode == ParticleEmitterVelocityMode::NextPoint) {
		changed |= MyGUI::Checkbox("折り返しで前の向きを使う", circle.usePrevSegmentDirectionOnWrap);
	}
	return changed;
}
