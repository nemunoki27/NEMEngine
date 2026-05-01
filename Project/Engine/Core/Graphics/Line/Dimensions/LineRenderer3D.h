#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Line/Base/LineRendererBase.h>
#include <Engine/Core/Graphics/Line/SceneGridRenderer.h>
#include <Engine/Core/Graphics/Mesh/GPUResource/MeshResourceTypes.h>

namespace Engine {

	//============================================================================
	//	LineRenderer3D class
	//	3Dライン描画を行うクラス
	//============================================================================
	class LineRenderer3D :
		public LineRendererBase<Vector3> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		LineRenderer3D(GraphicsCore& graphicsCore, RenderCameraDomain cameraDomain);
		~LineRenderer3D() = default;

		//---------- drawers -----------------------------------------------------

		// 球
		void DrawSphere(const Vector3& center, float radius, const Color4& color, uint32_t division = 8, float thickness = 1.0f);
		// 半球
		template <typename T>
		void DrawHemisphere(const Vector3& center, float radius, const T& rotation,
			const Color4& color, uint32_t division = 8, float thickness = 1.0f);
		// AABB
		void DrawAABB(const Vector3& min, const Vector3& max, const Color4& color, float thickness = 1.0f);
		// OBB
		template <typename T>
		void DrawOBB(const Vector3& center, const Vector3& size, const T& rotation, const Color4& color, float thickness = 1.0f);
		// コーン
		template <typename T>
		void DrawCone(const Vector3& center, float baseRadius, float topRadius, float height,
			const T& rotation, const Color4& color, uint32_t division = 8, float thickness = 1.0f);

		// 軸を描画(X軸(赤)Y軸(青)Z軸(緑))
		template <typename T>
		void DrawAxis(const Vector3& pos, const T& rotation, float length = 4.0f, float thickness = 1.0f);
		// スキンメッシュの骨を描画
		void DrawSkeleton(const Matrix4x4& worldMatrix, const Skeleton& skeleton);
		// カメラのフラスタムを描画
		void DrawCameraFrustum(const Matrix4x4& viewMatrix, float aspectRatio, float nearClip,
			float farClip, float fovY, float scale, const Color4& color, float thickness = 1.0f);
		// スポットライトのフラスタムを描画
		void DrawSpotLightFrustum(const Vector3& pos, const Vector3& direction, float distance, float cosAngle,
			float cosFalloffStart, const Color4& color, uint32_t division = 16, float thickness = 1.0f);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// シーンのグリッド描画クラス
		std::unique_ptr<SceneGridRenderer> gridRenderer_{};

		//--------- functions ----------------------------------------------------

		void DrawLineImpl(GraphicsCore& graphicsCore, const ResolvedCameraView* camera, MultiRenderTarget& surface) override;
	};

	//============================================================================
	//	LineRenderer3D templateMethods
	//============================================================================

	template<typename T>
	inline void LineRenderer3D::DrawHemisphere(const Vector3& center, float radius,
		const T& rotation, const Color4& color, uint32_t division, float thickness) {

		const float kLatEvery = (Math::pi / 2.0f) / division; // 緯度
		const float kLonEvery = 2.0f * Math::pi / division;   // 経度

		auto calculatePoint = [&](float lat, float lon) -> Vector3 {
			return {
				radius * std::cos(lat) * std::cos(lon),
				radius * std::sin(lat),
				radius * std::cos(lat) * std::sin(lon)
			};
			};
		Matrix4x4 rotationMatrix = Matrix4x4::Identity();
		if constexpr (std::is_same_v<T, Vector3>) {

			rotationMatrix = Matrix4x4::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Quaternion>) {

			rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Matrix4x4>) {

			rotationMatrix = rotation;
		}

		for (uint32_t latIndex = 0; latIndex < division; ++latIndex) {

			float lat = kLatEvery * latIndex;
			for (uint32_t lonIndex = 0; lonIndex < division; ++lonIndex) {
				float lon = lonIndex * kLonEvery;

				Vector3 pointA = calculatePoint(lat, lon);
				Vector3 pointB = calculatePoint(lat + kLatEvery, lon);
				Vector3 pointC = calculatePoint(lat, lon + kLonEvery);

				pointA = Vector3::TransformPoint(pointA, rotationMatrix) + center;
				pointB = Vector3::TransformPoint(pointB, rotationMatrix) + center;
				pointC = Vector3::TransformPoint(pointC, rotationMatrix) + center;

				DrawLine(pointA, pointB, color, thickness);
				DrawLine(pointA, pointC, color, thickness);
			}
		}
	}

	template<typename T>
	inline void LineRenderer3D::DrawOBB(const Vector3& center, const Vector3& size,
		const T& rotation, const Color4& color, float thickness) {

		const uint32_t vertexNum = 8;

		Matrix4x4 rotationMatrix = Matrix4x4::Identity();
		if constexpr (std::is_same_v<T, Vector3>) {

			rotationMatrix = Matrix4x4::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Quaternion>) {

			rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Matrix4x4>) {

			rotationMatrix = rotation;
		}

		Vector3 vertices[vertexNum];
		Vector3 halfSizeX = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), rotationMatrix) * size.x;
		Vector3 halfSizeY = Vector3::Transform(Vector3(0.0f, 1.0f, 0.0f), rotationMatrix) * size.y;
		Vector3 halfSizeZ = Vector3::Transform(Vector3(0.0f, 0.0f, 1.0f), rotationMatrix) * size.z;

		Vector3 offsets[vertexNum] = {
			{-1, -1, -1}, {-1,  1, -1}, {1, -1, -1}, {1,  1, -1},
			{-1, -1,  1}, {-1,  1,  1}, {1, -1,  1}, {1,  1,  1}
		};

		for (int i = 0; i < vertexNum; ++i) {

			Vector3 localVertex = offsets[i].x * halfSizeX +
				offsets[i].y * halfSizeY +
				offsets[i].z * halfSizeZ;
			vertices[i] = center + localVertex;
		}

		int edges[12][2] = {
			{0, 1}, {1, 3}, {3, 2}, {2, 0},
			{4, 5}, {5, 7}, {7, 6}, {6, 4},
			{0, 4}, {1, 5}, {2, 6}, {3, 7}
		};

		for (int i = 0; i < 12; ++i) {

			int start = edges[i][0];
			int end = edges[i][1];

			DrawLine(vertices[start], vertices[end], color, thickness);
		}
	}

	template<typename T>
	inline void LineRenderer3D::DrawCone(const Vector3& center, float baseRadius, float topRadius,
		float height, const T& rotation, const Color4& color, uint32_t division, float thickness) {

		const float kAngleStep = 2.0f * Math::pi / division;

		std::vector<Vector3> baseCircle;
		std::vector<Vector3> topCircle;

		// 基底円と上面円の計算
		for (uint32_t i = 0; i <= division; ++i) {

			float angle = i * kAngleStep;
			baseCircle.emplace_back(baseRadius * std::cos(angle), 0.0f, baseRadius * std::sin(angle));
			topCircle.emplace_back(topRadius * std::cos(angle), height, topRadius * std::sin(angle));
		}

		Matrix4x4 rotationMatrix = Matrix4x4::Identity();
		if constexpr (std::is_same_v<T, Vector3>) {

			rotationMatrix = Matrix4x4::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Quaternion>) {

			rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Matrix4x4>) {

			rotationMatrix = rotation;
		}

		for (uint32_t i = 0; i < division; ++i) {

			// 円周上の点を回転＆平行移動
			Vector3 baseA = Vector3::TransformPoint(baseCircle[i], rotationMatrix) + center;
			Vector3 baseB = Vector3::TransformPoint(baseCircle[i + 1], rotationMatrix) + center;
			Vector3 topA = Vector3::TransformPoint(topCircle[i], rotationMatrix) + center;
			Vector3 topB = Vector3::TransformPoint(topCircle[i + 1], rotationMatrix) + center;

			// 円の描画
			DrawLine(baseA, baseB, color, thickness);
			DrawLine(topA, topB, color, thickness);

			// 側面の描画
			DrawLine(baseA, topA, color, thickness);
		}
	}

	template<typename T>
	inline void LineRenderer3D::DrawAxis(const Vector3& pos, const T& rotation, float length, float thickness) {

		// X軸(赤)Y軸(青)Z軸(緑)
		// 回転
		Matrix4x4 rotationMatrix = Matrix4x4::Identity();
		if constexpr (std::is_same_v<T, Vector3>) {

			rotationMatrix = Matrix4x4::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Quaternion>) {

			rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Matrix4x4>) {

			rotationMatrix = rotation;
		}

		// 基準軸を回転させて、ワールド空間の軸方向にする
		Vector3 xDirection = Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotationMatrix).Normalize();
		Vector3 yDirection = Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotationMatrix).Normalize();
		Vector3 zDirection = Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotationMatrix).Normalize();

		DrawLine(pos, pos + xDirection * length, Color4::Red(), thickness);
		DrawLine(pos, pos + yDirection * length, Color4::Blue(), thickness);
		DrawLine(pos, pos + zDirection * length, Color4::Green(), thickness);
	}
} // Engine