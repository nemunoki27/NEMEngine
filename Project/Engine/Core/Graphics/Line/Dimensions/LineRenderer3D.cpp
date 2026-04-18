#include "LineRenderer3D.h"

//============================================================================
//	LineRenderer3D classMethods
//============================================================================

Engine::LineRenderer3D::LineRenderer3D(GraphicsCore& graphicsCore, RenderCameraDomain cameraDomain) {

	// 基底クラスの初期化
	Init(graphicsCore, cameraDomain);

	// シーンのグリッド描画クラス初期化
	gridRenderer_ = std::make_unique<SceneGridRenderer>();
	gridRenderer_->Init(graphicsCore);
}

void Engine::LineRenderer3D::DrawSphere(const Vector3& center, float radius,
	const Color& color, uint32_t division, float thickness) {

	const float kLatEvery = Math::pi / division;        // 緯度
	const float kLonEvery = 2.0f * Math::pi / division; // 経度

	auto calculatePoint = [&](float lat, float lon) -> Vector3 {
		return {
			radius * std::cos(lat) * std::cos(lon),
			radius * std::sin(lat),
			radius * std::cos(lat) * std::sin(lon)
		};
		};
	for (uint32_t latIndex = 0; latIndex < division; ++latIndex) {

		float lat = -Math::pi / 2.0f + kLatEvery * latIndex;
		for (uint32_t lonIndex = 0; lonIndex < division; ++lonIndex) {
			float lon = lonIndex * kLonEvery;

			Vector3 pointA = calculatePoint(lat, lon);
			Vector3 pointB = calculatePoint(lat + kLatEvery, lon);
			Vector3 pointC = calculatePoint(lat, lon + kLonEvery);

			DrawLine(pointA + center, pointB + center, color, thickness);
			DrawLine(pointA + center, pointC + center, color, thickness);
		}
	}
}

void Engine::LineRenderer3D::DrawAABB(const Vector3& min, const Vector3& max, const Color& color, float thickness) {

	// AABBの各頂点
	std::vector<Vector3> vertices = {
		{min.x, min.y, min.z},
		{max.x, min.y, min.z},
		{min.x, max.y, min.z},
		{max.x, max.y, min.z},
		{min.x, min.y, max.z},
		{max.x, min.y, max.z},
		{min.x, max.y, max.z},
		{max.x, max.y, max.z} };

	// 各辺
	std::vector<std::pair<int, int>> edges = {
		{0, 1}, {1, 3}, {3, 2}, {2, 0}, // 前面
		{4, 5}, {5, 7}, {7, 6}, {6, 4}, // 背面
		{0, 4}, {1, 5}, {2, 6}, {3, 7}  // 前面と背面を繋ぐ辺
	};

	for (const auto& edge : edges) {

		const Vector3& start = vertices[edge.first];
		const Vector3& end = vertices[edge.second];

		// 各辺の描画
		DrawLine(start, end, color, thickness);
	}
}

void Engine::LineRenderer3D::DrawSkeleton(const Matrix4x4& worldMatrix, const Skeleton& skeleton) {

	// ジョイントがない場合は描画しない
	if (skeleton.joints.empty()) {
		return;
	}

	// ジョイントの親子関係を構築
	std::vector<std::vector<int32_t>> children(skeleton.joints.size());
	for (size_t i = 0; i < skeleton.joints.size(); ++i) {
		if (skeleton.joints[i].parent) {

			children[*skeleton.joints[i].parent].push_back(static_cast<int32_t>(i));
		}
	}

	// 秒が定数
	constexpr int32_t kDivision = 4;
	constexpr float kRatioTop = 0.02f;
	constexpr float kRatioBase = 0.088f;

	// ジョイント位置の描画
	std::vector<Vector3> worldPos(skeleton.joints.size());
	for (size_t i = 0; i < skeleton.joints.size(); ++i) {

		worldPos[i] = Vector3::Transform(Vector3::AnyInit(0.0f), skeleton.joints[i].skeletonSpaceMatrix * worldMatrix);
		DrawSphere(worldPos[i], 0.02f, Color::Yellow(), kDivision, 1.0f);
	}
	// 親から子に向けて描画
	for (size_t parent = 0; parent < skeleton.joints.size(); ++parent) {
		for (int32_t child : children[parent]) {

			Vector3 base = worldPos[parent]; // 親
			Vector3 tip = worldPos[child];   // 子
			Vector3 direction = tip - base;
			float length = direction.Length();
			if (length < 1e-4f) {
				continue;
			}

			direction.x /= length;
			direction.y /= length;
			direction.z /= length;
			Quaternion rotation = Quaternion::FromToY(direction);
			rotation = rotation.Inverse(rotation);

			float top = length * kRatioTop;
			float bottom = length * kRatioBase;

			DrawCone(base, bottom, top, length, rotation, Color::Yellow(), kDivision, 1.0f);
		}
	}
}

void Engine::LineRenderer3D::DrawSpotLightFrustum(const Vector3& pos, const Vector3& direction, \
	float distance, float cosAngle, float cosFalloffStart, const Color& color, uint32_t division, float thickness) {

	// 方向正規化
	Vector3 lightDir = direction;
	float dirLength = lightDir.Length();
	if (dirLength <= 1e-4f) {
		return;
	}
	lightDir.x /= dirLength;
	lightDir.y /= dirLength;
	lightDir.z /= dirLength;

	// cos -> angleの範囲外補正
	cosAngle = (cosAngle < -1.0f) ? -1.0f : ((cosAngle > 1.0f) ? 1.0f : cosAngle);
	cosFalloffStart = (cosFalloffStart < -1.0f) ? -1.0f : ((cosFalloffStart > 1.0f) ? 1.0f : cosFalloffStart);

	// アングルからコーンの半径を計算
	float outerAngle = std::acos(cosAngle);
	float innerAngle = std::acos(cosFalloffStart);
	float outerRadius = std::tan(outerAngle) * distance;
	float innerRadius = std::tan(innerAngle) * distance;

	Quaternion rotation = Quaternion::FromToY(lightDir);
	rotation = rotation.Inverse(rotation);

	// 外側コーン
	DrawCone(pos, 0.0f, outerRadius, distance, rotation, color, division, thickness);

	// 中心線
	const Vector3 farCenter = pos + lightDir * distance;
	DrawLine(pos, farCenter, color, thickness);

	// フォールオフ開始角度がコーンの角度より小さい場合、内側のリングとガイド線を描画
	if (cosAngle + 1e-4f < cosFalloffStart) {

		Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
		float kAngleStep = 2.0f * Math::pi / static_cast<float>(division);

		std::vector<Vector3> innerCircle;
		innerCircle.reserve(division);
		for (uint32_t i = 0; i < division; ++i) {

			float angle = static_cast<float>(i) * kAngleStep;

			// 内側リングの点を計算
			Vector3 localPoint(innerRadius * std::cos(angle),
				distance, innerRadius * std::sin(angle));
			innerCircle.emplace_back(Vector3::TransformPoint(localPoint, rotationMatrix) + pos);
		}

		// 内側リング
		for (uint32_t i = 0; i < division; ++i) {

			Vector3 a = innerCircle[i];
			Vector3 b = innerCircle[(i + 1) % division];
			DrawLine(a, b, color, thickness);
		}

		// 内側コーンのガイド線を4本だけ引く
		uint32_t kGuideCount = 4;
		for (uint32_t i = 0; i < kGuideCount; ++i) {

			uint32_t index = (division * i) / kGuideCount;
			DrawLine(pos, innerCircle[index], color, thickness);
		}
	}
}

void Engine::LineRenderer3D::DrawCameraFrustum(const Matrix4x4& viewMatrix, float aspectRatio,
	float nearClip, float farClip, float fovY, float scale, const Color& color, float thickness) {

	// カメラ空間でのコーナー計算
	float halfFovY = (fovY + 0.08f) * 0.5f;
	float heightNearHalf = std::tan(halfFovY) * nearClip;
	float widthNearHalf = heightNearHalf * aspectRatio;
	float heightFarHalf = std::tan(halfFovY) * farClip;
	float widthFarHalf = heightFarHalf * aspectRatio;

	Vector3 ncTL(-widthNearHalf, heightNearHalf, nearClip);
	Vector3 ncTR(widthNearHalf, heightNearHalf, nearClip);
	Vector3 ncBR(widthNearHalf, -heightNearHalf, nearClip);
	Vector3 ncBL(-widthNearHalf, -heightNearHalf, nearClip);

	Vector3 fcTL(-widthFarHalf, heightFarHalf, farClip);
	Vector3 fcTR(widthFarHalf, heightFarHalf, farClip);
	Vector3 fcBR(widthFarHalf, -heightFarHalf, farClip);
	Vector3 fcBL(-widthFarHalf, -heightFarHalf, farClip);

	ncTL *= scale;
	ncTR *= scale;
	ncBR *= scale;
	ncBL *= scale;

	fcTL *= scale;
	fcTR *= scale;
	fcBR *= scale;
	fcBL *= scale;

	Matrix4x4 cameraWorldMatrix = Matrix4x4::Inverse(viewMatrix);

	// ワールド座標に変換
	Vector3 wncTL = Vector3::Transform(ncTL, cameraWorldMatrix);
	Vector3 wncTR = Vector3::Transform(ncTR, cameraWorldMatrix);
	Vector3 wncBR = Vector3::Transform(ncBR, cameraWorldMatrix);
	Vector3 wncBL = Vector3::Transform(ncBL, cameraWorldMatrix);

	Vector3 wfcTL = Vector3::Transform(fcTL, cameraWorldMatrix);
	Vector3 wfcTR = Vector3::Transform(fcTR, cameraWorldMatrix);
	Vector3 wfcBR = Vector3::Transform(fcBR, cameraWorldMatrix);
	Vector3 wfcBL = Vector3::Transform(fcBL, cameraWorldMatrix);

	// 近クリップ
	DrawLine(wncTL, wncTR, color, thickness);
	DrawLine(wncTR, wncBR, color, thickness);
	DrawLine(wncBR, wncBL, color, thickness);
	DrawLine(wncBL, wncTL, color, thickness);
	// 遠クリップ
	DrawLine(wfcTL, wfcTR, color, thickness);
	DrawLine(wfcTR, wfcBR, color, thickness);
	DrawLine(wfcBR, wfcBL, color, thickness);
	DrawLine(wfcBL, wfcTL, color, thickness);
	// 近 → 遠
	DrawLine(wncTL, wfcTL, color, thickness);
	DrawLine(wncTR, wfcTR, color, thickness);
	DrawLine(wncBR, wfcBR, color, thickness);
	DrawLine(wncBL, wfcBL, color, thickness);
}

void Engine::LineRenderer3D::DrawLineImpl(GraphicsCore& graphicsCore,
	const ResolvedCameraView* camera, MultiRenderTarget& surface) {

	// グリッド描画
	gridRenderer_->Render(graphicsCore, *camera, surface);
}