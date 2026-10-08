#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <cmath>
#include <vector>

namespace Engine {

	//============================================================================
	//	LineShapeBuilder namespace
	//	組み込み形状を即時描画とデバッグ描画の線分へ展開する
	//============================================================================
	namespace LineShapeBuilder {

		// 球
		void BuildSphere(const Vector3& center, float radius, const Color4& color, uint32_t division, float thickness,
			std::vector<LinePoint>& out);
		// 半球、rotationで向きを変える
		void BuildHemisphere(const Vector3& center, float radius, const Quaternion& rotation, const Color4& color,
			uint32_t division, float thickness, std::vector<LinePoint>& out);
		// 軸平行ボックス
		void BuildAABB(
			const Vector3& min, const Vector3& max, const Color4& color, float thickness, std::vector<LinePoint>& out);
		// 有向ボックス、sizeは各軸の半径
		void BuildOBB(const Vector3& center, const Vector3& size, const Quaternion& rotation, const Color4& color,
			float thickness, std::vector<LinePoint>& out);
		// 円錐台、baseRadiusとtopRadiusとheight
		void BuildCone(const Vector3& center, float baseRadius, float topRadius, float height, const Quaternion& rotation,
			const Color4& color, uint32_t division, float thickness, std::vector<LinePoint>& out);
		// 方向矢印
		void BuildArrow(const Vector3& pos, float length, const Quaternion& rotation, const Color4& color, float thickness,
			std::vector<LinePoint>& out);
		// XYZ軸をX赤Y青Z緑で生成する
		void BuildAxis(
			const Vector3& pos, const Quaternion& rotation, float length, float thickness, std::vector<LinePoint>& out);

		// 2D円、XY平面
		void BuildCircle2D(const Vector2& center, float radius, const Color4& color, uint32_t division, float thickness,
			std::vector<LinePoint>& out);
		// 2D矩形を回転してXY平面へ配置
		void BuildRect2D(const Vector2& center, const Vector2& size, const Quaternion& rotation, const Color4& color,
			float thickness, std::vector<LinePoint>& out);

		namespace Detail {

			// 緯度と経度から球面上の点を取得
			Vector3 SphericalPoint(float radius, float latitude, float longitude);
			// 分割数を三つ以上に揃える
			uint32_t SafeDivision(uint32_t division);
		}

		// 球面の緯線と経線を出力
		template<typename Emit>
		void ForEachSphereLine(const Vector3& center, float radius, uint32_t division, Emit&& emit) {

			// 緯度と経度の分割間隔を求める
			const uint32_t latDivision = Detail::SafeDivision(division);
			const float kLatEvery = Math::pi / static_cast<float>(latDivision);
			const float kLonEvery = 2.0f * Math::pi / static_cast<float>(latDivision);

			for (uint32_t latIndex = 0; latIndex < latDivision; ++latIndex) {

				const float lat = -Math::pi / 2.0f + kLatEvery * static_cast<float>(latIndex);
				for (uint32_t lonIndex = 0; lonIndex < latDivision; ++lonIndex) {

					const float lon = static_cast<float>(lonIndex) * kLonEvery;
					const Vector3 pointA = Detail::SphericalPoint(radius, lat, lon);
					const Vector3 pointB = Detail::SphericalPoint(radius, lat + kLatEvery, lon);
					const Vector3 pointC = Detail::SphericalPoint(radius, lat, lon + kLonEvery);

					emit(center + pointA, center + pointB);
					emit(center + pointA, center + pointC);
				}
			}
		}

		// 回転した半球の緯線と経線を出力
		template<typename Emit>
		void ForEachHemisphereLine(const Vector3& center, float radius, const Matrix4x4& rotationMatrix,
			uint32_t division, Emit&& emit) {

			// 分割数の下限と角度間隔を準備
			const uint32_t div = Detail::SafeDivision(division);
			const float kLatEvery = (Math::pi / 2.0f) / static_cast<float>(div);
			const float kLonEvery = 2.0f * Math::pi / static_cast<float>(div);
			for (uint32_t latIndex = 0; latIndex < div; ++latIndex) {

				const float lat = kLatEvery * static_cast<float>(latIndex);
				for (uint32_t lonIndex = 0; lonIndex < div; ++lonIndex) {

					const float lon = static_cast<float>(lonIndex) * kLonEvery;
					Vector3 pointA = Vector3::Transform(Detail::SphericalPoint(radius, lat, lon), rotationMatrix) + center;
					Vector3 pointB = Vector3::Transform(Detail::SphericalPoint(radius, lat + kLatEvery, lon), rotationMatrix) + center;
					Vector3 pointC = Vector3::Transform(Detail::SphericalPoint(radius, lat, lon + kLonEvery), rotationMatrix) + center;

					emit(pointA, pointB);
					emit(pointA, pointC);
				}
			}
		}

		// 回転した箱の十二辺を出力
		template<typename Emit>
		void ForEachOBBLine(const Vector3& center, const Vector3& size, const Matrix4x4& rotationMatrix, Emit&& emit) {

			const Vector3 halfX = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), rotationMatrix) * size.x;
			const Vector3 halfY = Vector3::Transform(Vector3(0.0f, 1.0f, 0.0f), rotationMatrix) * size.y;
			const Vector3 halfZ = Vector3::Transform(Vector3(0.0f, 0.0f, 1.0f), rotationMatrix) * size.z;

			// 回転後の八頂点を作成
			const Vector3 offsets[8] = {{-1.0f, -1.0f, -1.0f}, {-1.0f, 1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, -1.0f},
				{-1.0f, -1.0f, 1.0f}, {-1.0f, 1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
			Vector3 vertices[8];
			for (int i = 0; i < 8; ++i) {
				vertices[i] = center + offsets[i].x * halfX + offsets[i].y * halfY + offsets[i].z * halfZ;
			}

			// 箱の十二辺を線分へ展開
			const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7}, {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
			for (int i = 0; i < 12; ++i) {
				emit(vertices[edges[i][0]], vertices[edges[i][1]]);
			}
		}

		// 円錐台の上下輪郭と側面を出力
		template<typename Emit>
		void ForEachConeLine(const Vector3& center, float baseRadius, float topRadius, float height,
			const Matrix4x4& rotationMatrix, uint32_t division, Emit&& emit) {

			// 分割数の下限と角度間隔を準備
			const uint32_t div = Detail::SafeDivision(division);
			const float kAngleStep = 2.0f * Math::pi / static_cast<float>(div);
			for (uint32_t i = 0; i < div; ++i) {

				const float angle0 = static_cast<float>(i) * kAngleStep;
				const float angle1 = static_cast<float>(i + 1) * kAngleStep;

				const Vector3 base0 =
					Vector3::Transform(Vector3(baseRadius * std::cos(angle0), 0.0f, baseRadius * std::sin(angle0)), rotationMatrix) +
					center;
				const Vector3 base1 =
					Vector3::Transform(Vector3(baseRadius * std::cos(angle1), 0.0f, baseRadius * std::sin(angle1)), rotationMatrix) +
					center;
				const Vector3 top0 =
					Vector3::Transform(Vector3(topRadius * std::cos(angle0), height, topRadius * std::sin(angle0)), rotationMatrix) +
					center;
				const Vector3 top1 =
					Vector3::Transform(Vector3(topRadius * std::cos(angle1), height, topRadius * std::sin(angle1)), rotationMatrix) +
					center;

				emit(base0, base1);
				emit(top0, top1);
				emit(base0, top0);
			}
		}


		// 平面の円周を閉じた線分として出力
		template<typename Emit>
		void ForEachCircle2DLine(const Vector2& center, float radius, uint32_t division, Emit&& emit) {

			const uint32_t div = Detail::SafeDivision(division);
			const float step = 2.0f * Math::pi / static_cast<float>(div);
			const auto point = [&](uint32_t index) {

				const float angle = static_cast<float>(index) * step;
				return Vector2(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
			};
			// 終端を始点に揃えて円周を閉じる
			for (uint32_t index = 0; index < div; ++index) {

				emit(point(index), point((index + 1) % div));
			}
		}

		// 回転した矩形の四辺を平面へ出力
		template<typename Emit>
		void ForEachRect2DLine(const Vector2& center, const Vector2& half, const Matrix4x4& rotationMatrix, Emit&& emit) {

			const auto corner = [&](float x, float y) {

				const Vector3 rotated = Vector3::Transform(Vector3(x * half.x, y * half.y, 0.0f), rotationMatrix);
				return Vector2(center.x + rotated.x, center.y + rotated.y);
			};
			// 回転後の四隅を順に接続
			const Vector2 corners[4] = { corner(-1.0f, -1.0f), corner(1.0f, -1.0f),
				corner(1.0f, 1.0f), corner(-1.0f, 1.0f) };
			for (uint32_t index = 0; index < 4; ++index) {

				emit(corners[index], corners[(index + 1) % 4]);
			}
		}

		// 矢印の線分を指定した出力先へ渡す
		template<typename Emit>
		void ForEachArrowLine(const Vector3& pos, float length, const Matrix4x4& rotationMatrix, Emit&& emit) {

			if (length <= 0.0f) {
				return;
			}

			constexpr uint32_t kDivision = 16;
			constexpr uint32_t kAxisCount = 4;
			const float shaftLength = length * 0.72f;
			const float shaftRadius = length * 0.035f;
			const float headRadius = length * 0.11f;

			const Vector3 right = Vector3::NormalizeOr(
				Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotationMatrix),
				Vector3(1.0f, 0.0f, 0.0f));
			const Vector3 up = Vector3::NormalizeOr(
				Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotationMatrix),
				Vector3(0.0f, 1.0f, 0.0f));
			const Vector3 forward = Vector3::NormalizeOr(
				Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotationMatrix),
				Vector3(0.0f, 0.0f, 1.0f));

			auto makePoint = [&](float y, float radius, float angle) {

				return pos + up * y + right * (std::cos(angle) * radius) + forward * (std::sin(angle) * radius);
			};

			// 円柱の上下輪郭を生成
			const float kEvery = 2.0f * Math::pi / static_cast<float>(kDivision);
			for (uint32_t i = 0; i < kDivision; ++i) {

				const float angle0 = kEvery * static_cast<float>(i);
				const float angle1 = kEvery * static_cast<float>(i + 1);
				emit(makePoint(0.0f, shaftRadius, angle0), makePoint(0.0f, shaftRadius, angle1));
				emit(makePoint(shaftLength, shaftRadius, angle0), makePoint(shaftLength, shaftRadius, angle1));
			}

			for (uint32_t i = 0; i < kAxisCount; ++i) {

				const float angle = (2.0f * Math::pi / static_cast<float>(kAxisCount)) * static_cast<float>(i);
				emit(makePoint(0.0f, shaftRadius, angle), makePoint(shaftLength, shaftRadius, angle));
			}

			// 円錐の輪郭と先端を生成
			const Vector3 tip = pos + up * length;
			for (uint32_t i = 0; i < kDivision; ++i) {

				const float angle0 = kEvery * static_cast<float>(i);
				const float angle1 = kEvery * static_cast<float>(i + 1);
				emit(makePoint(shaftLength, headRadius, angle0), makePoint(shaftLength, headRadius, angle1));
			}
			for (uint32_t i = 0; i < kAxisCount; ++i) {

				const float angle = (2.0f * Math::pi / static_cast<float>(kAxisCount)) * static_cast<float>(i);
				emit(makePoint(shaftLength, headRadius, angle), tip);
			}
		}

		// 回転したXYZ軸を指定した出力先へ渡す
		template<typename Emit>
		void ForEachAxisLine(const Vector3& pos, float length, const Matrix4x4& rotationMatrix, Emit&& emit) {

			// 各軸の向きと色を揃える
			Vector3 xDirection = Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotationMatrix).Normalize();
			Vector3 yDirection = Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotationMatrix).Normalize();
			Vector3 zDirection = Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotationMatrix).Normalize();

			emit(pos, pos + xDirection * length, Color4::Red());
			emit(pos, pos + yDirection * length, Color4::Blue());
			emit(pos, pos + zDirection * length, Color4::Green());
		}

	}
}
