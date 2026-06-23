#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>
#include <Engine/Core/Foundation/Math/Color.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	LineShapeBuilder namespace
	//	組み込み形状を線分リスト(2点ずつ独立)としてLinePointへ展開する、即時描画とデバッグ描画で共有できる
	//============================================================================
	namespace LineShapeBuilder {

		// 球
		void BuildSphere(const Vector3& center, float radius, const Color4& color,
			uint32_t division, float thickness, std::vector<LinePoint>& out);
		// 半球、rotationで向きを変える
		void BuildHemisphere(const Vector3& center, float radius, const Quaternion& rotation,
			const Color4& color, uint32_t division, float thickness, std::vector<LinePoint>& out);
		// 軸平行ボックス
		void BuildAABB(const Vector3& min, const Vector3& max, const Color4& color,
			float thickness, std::vector<LinePoint>& out);
		// 有向ボックス、sizeは各軸の半径
		void BuildOBB(const Vector3& center, const Vector3& size, const Quaternion& rotation,
			const Color4& color, float thickness, std::vector<LinePoint>& out);
		// 円錐台、baseRadiusとtopRadiusとheight
		void BuildCone(const Vector3& center, float baseRadius, float topRadius, float height,
			const Quaternion& rotation, const Color4& color, uint32_t division, float thickness, std::vector<LinePoint>& out);
		// 方向矢印
		void BuildArrow(const Vector3& pos, float length, const Quaternion& rotation,
			const Color4& color, float thickness, std::vector<LinePoint>& out);
		// XYZ軸、X赤Y緑Z青で色は固定
		void BuildAxis(const Vector3& pos, const Quaternion& rotation, float length,
			float thickness, std::vector<LinePoint>& out);

		// 2D円、XY平面
		void BuildCircle2D(const Vector2& center, float radius, const Color4& color,
			uint32_t division, float thickness, std::vector<LinePoint>& out);
		// 2D矩形、rotationはZ回転として扱う
		void BuildRect2D(const Vector2& center, const Vector2& size, const Quaternion& rotation,
			const Color4& color, float thickness, std::vector<LinePoint>& out);
	}
} // Engine
