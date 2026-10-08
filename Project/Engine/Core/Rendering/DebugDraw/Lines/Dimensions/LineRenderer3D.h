#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DebugDraw/Lines/Base/LineRendererBase.h>
#include <Engine/Core/Rendering/DebugDraw/Lines/SceneGridRenderer.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>

// c++
#include <type_traits>

namespace Engine {

	//============================================================================
	//	LineRenderer3D class
	//	3Dライン描画を行うクラス
	//============================================================================
	class LineRenderer3D :
		public LineRendererBase<Vector3> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		LineRenderer3D(GraphicsCore& graphicsCore, RenderCameraDomain cameraDomain);
		~LineRenderer3D() override;

		// フレーム開始処理
		void BeginFrame();

		//---------- drawers -----------------------------------------------------

		// 指定した間隔かCamera距離に応じたグリッドを描画
		void DrawGrid(float minorStep = 0.0f);
		// Sceneの深度を使ってグリッドを描画
		void RenderDefaultGrid(GraphicsCore& graphicsCore, const ResolvedRenderView& view, MultiRenderTarget& surface,
			DepthTexture2D* occlusionDepth);
		// 次のScene描画に使うグリッドの深度を設定
		void SetSnapGridOcclusionDepth(DepthTexture2D* depth) { snapGridOcclusionDepth_ = depth; }

		// 球の緯線と経線を描く
		void DrawSphereGrid(const Vector3& center, float radius, const Color4& color, uint32_t division = 8, float thickness = 1.0f);
		// 球の中心を通る3つの円を描く
		void DrawSphere(const Vector3& center, float radius, const Color4& color, float thickness = 1.0f);
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
		// 円柱と円錐で構成した方向矢印を描画
		template <typename T>
		void DrawArrow(const Vector3& pos, float length, const T& rotation,
			const Color4& color, float thickness = 1.0f);

		// X軸を赤、Y軸を青、Z軸を緑で描く
		template <typename T>
		void DrawAxis(const Vector3& pos, const T& rotation, float length = 4.0f, float thickness = 1.0f);
		// スキンメッシュの骨を描画
		void DrawSkeleton(const Matrix4x4& worldMatrix, const Skeleton& skeleton);
		// カメラのフラスタムを描画
		void DrawCameraFrustum(const Matrix4x4& viewMatrix, float aspectRatio, float nearClip,
			float farClip, float fovY, float scale, const Color4& color, float thickness = 1.0f);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// シーンのグリッド描画クラス
		std::unique_ptr<SceneGridRenderer> gridRenderer_{};
		// DrawGridで要求されたグリッド描画数
		uint32_t gridDrawCount_ = 0;
		// DrawGridで指定された固定グリッド間隔、0なら自動フィット
		float gridMinorStep_ = 0.0f;
		// 現在Frameで借用するグリッドの深度
		DepthTexture2D* snapGridOcclusionDepth_ = nullptr;

		//--------- functions ----------------------------------------------------

		// 回転の入力形式を行列へ揃える
		template <typename T>
		static Matrix4x4 ResolveRotationMatrix(const T& rotation);

		// 指定カメラへ線とグリッドを描く
		void DrawLineImpl(GraphicsCore& graphicsCore, const ResolvedCameraView* camera, MultiRenderTarget& surface) override;
	};

	//============================================================================
	//	LineRenderer3D templateMethods
	//============================================================================
	template <typename T>
	Matrix4x4 LineRenderer3D::ResolveRotationMatrix(const T& rotation) {

		if constexpr (std::is_same_v<T, Vector3>) {
			return Matrix4x4::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Quaternion>) {
			return Quaternion::MakeRotateMatrix(rotation);
		} else if constexpr (std::is_same_v<T, Matrix4x4>) {
			return rotation;
		} else {
			return Matrix4x4::Identity();
		}
	}

	template<typename T>
	inline void LineRenderer3D::DrawHemisphere(const Vector3& center, float radius,
		const T& rotation, const Color4& color, uint32_t division, float thickness) {

		// 共通の形状から描画用の線を追加
		LineShapeBuilder::ForEachHemisphereLine(center, radius, ResolveRotationMatrix(rotation), division,
			[&](const Vector3& start, const Vector3& end) {
				DrawLine(start, end, color, thickness);
			});
	}

	template<typename T>
	inline void LineRenderer3D::DrawOBB(const Vector3& center, const Vector3& size,
		const T& rotation, const Color4& color, float thickness) {

		// 共通の形状から描画用の線を追加
		LineShapeBuilder::ForEachOBBLine(center, size, ResolveRotationMatrix(rotation),
			[&](const Vector3& start, const Vector3& end) {
				DrawLine(start, end, color, thickness);
			});
	}

	template<typename T>
	inline void LineRenderer3D::DrawCone(const Vector3& center, float baseRadius, float topRadius,
		float height, const T& rotation, const Color4& color, uint32_t division, float thickness) {

		// 共通の形状から描画用の線を追加
		LineShapeBuilder::ForEachConeLine(center, baseRadius, topRadius, height, ResolveRotationMatrix(rotation), division,
			[&](const Vector3& start, const Vector3& end) {
				DrawLine(start, end, color, thickness);
			});
	}

	template<typename T>
	inline void LineRenderer3D::DrawArrow(const Vector3& pos, float length, const T& rotation,
		const Color4& color, float thickness) {

		// 共通の形状生成から描画用の線を追加
		LineShapeBuilder::ForEachArrowLine(pos, length, ResolveRotationMatrix(rotation),
			[&](const Vector3& start, const Vector3& end) {
				DrawLine(start, end, color, thickness);
			});
	}

	template<typename T>
	inline void LineRenderer3D::DrawAxis(const Vector3& pos, const T& rotation, float length, float thickness) {

		// 共通の形状生成から描画用の線を追加
		LineShapeBuilder::ForEachAxisLine(pos, length, ResolveRotationMatrix(rotation),
			[&](const Vector3& start, const Vector3& end, const Color4& lineColor) {
				DrawLine(start, end, lineColor, thickness);
			});
	}
}

