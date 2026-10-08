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

Engine::LineRenderer3D::~LineRenderer3D() {

	gridRenderer_.reset();
}

void Engine::LineRenderer3D::BeginFrame() {

	LineRendererBase<Vector3>::BeginFrame();
	gridDrawCount_ = 0;
	gridMinorStep_ = 0.0f;
	snapGridOcclusionDepth_ = nullptr;
	gridRenderer_->BeginFrame();
}

void Engine::LineRenderer3D::DrawGrid(float minorStep) {

	++gridDrawCount_;
	gridMinorStep_ = minorStep;
}

void Engine::LineRenderer3D::RenderDefaultGrid(GraphicsCore& graphicsCore,
	const ResolvedRenderView& view, MultiRenderTarget& surface, DepthTexture2D* occlusionDepth) {

	const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective);
	if (!camera) {
		return;
	}

	// SceneViewのカメラでデフォルトグリッドを描画、シーン深度を渡してメッシュに隠れるようにする
	gridRenderer_->Render(graphicsCore, *camera, surface, 0.0f, occlusionDepth);
}

void Engine::LineRenderer3D::DrawSphereGrid(const Vector3& center, float radius,
	const Color4& color, uint32_t division, float thickness) {

	// 共通の形状から描画用の線を追加
	LineShapeBuilder::ForEachSphereLine(center, radius, division,
		[&](const Vector3& start, const Vector3& end) {
			DrawLine(start, end, color, thickness);
		});
}

void Engine::LineRenderer3D::DrawSphere(const Vector3& center, float radius, const Color4& color, float thickness) {

	const uint32_t kDivision = 32;
	const float kEvery = 2.0f * Math::pi / static_cast<float>(kDivision);

	for (uint32_t index = 0; index < kDivision; ++index) {
		float t0 = kEvery * static_cast<float>(index);
		float t1 = kEvery * static_cast<float>(index + 1);

		// 緯度の中心線：赤道XZ平面
		Vector3 equatorA = {
			center.x + radius * std::cos(t0),
			center.y,
			center.z + radius * std::sin(t0)
		};

		Vector3 equatorB = {
			center.x + radius * std::cos(t1),
			center.y,
			center.z + radius * std::sin(t1)
		};

		DrawLine(equatorA, equatorB, color, thickness);

		// 経度の中心線：縦方向の大円XY平面
		Vector3 meridianA = {
			center.x + radius * std::cos(t0),
			center.y + radius * std::sin(t0),
			center.z
		};

		Vector3 meridianB = {
			center.x + radius * std::cos(t1),
			center.y + radius * std::sin(t1),
			center.z
		};

		DrawLine(meridianA, meridianB, color, thickness);

		// もう1本の経度線：YZ平面
		Vector3 meridian2A = {
			center.x,
			center.y + radius * std::sin(t0),
			center.z + radius * std::cos(t0)
		};

		Vector3 meridian2B = {
			center.x,
			center.y + radius * std::sin(t1),
			center.z + radius * std::cos(t1)
		};

		DrawLine(meridian2A, meridian2B, color, thickness);
	}
}

void Engine::LineRenderer3D::DrawAABB(const Vector3& min, const Vector3& max, const Color4& color, float thickness) {

	// 中心と半径に揃えて箱の辺を生成
	LineShapeBuilder::ForEachOBBLine((min + max) * 0.5f, (max - min) * 0.5f, Matrix4x4::Identity(),
		[&](const Vector3& start, const Vector3& end) {
			DrawLine(start, end, color, thickness);
		});
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

	// 骨の分割数と太さを揃える
	constexpr int32_t kDivision = 4;
	constexpr float kRatioTop = 0.02f;
	constexpr float kRatioBase = 0.088f;

	// ジョイント位置の描画
	std::vector<Vector3> worldPos(skeleton.joints.size());
	for (size_t i = 0; i < skeleton.joints.size(); ++i) {

		worldPos[i] = Vector3::Transform(Vector3::AnyInit(0.0f), skeleton.joints[i].skeletonSpaceMatrix * worldMatrix);
		DrawSphereGrid(worldPos[i], 0.02f, Color4::Yellow(), kDivision, 1.0f);
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

			float top = length * kRatioTop;
			float bottom = length * kRatioBase;

			DrawCone(base, bottom, top, length, rotation, Color4::Yellow(), kDivision, 1.0f);
		}
	}
}

void Engine::LineRenderer3D::DrawCameraFrustum(const Matrix4x4& viewMatrix, float aspectRatio,
	float nearClip, float farClip, float fovY, float scale, const Color4& color, float thickness) {

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
	// 近→遠
	DrawLine(wncTL, wfcTL, color, thickness);
	DrawLine(wncTR, wfcTR, color, thickness);
	DrawLine(wncBR, wfcBR, color, thickness);
	DrawLine(wncBL, wfcBL, color, thickness);
}

void Engine::LineRenderer3D::DrawLineImpl(GraphicsCore& graphicsCore,
	const ResolvedCameraView* camera, MultiRenderTarget& surface) {

	if (!camera) {
		gridDrawCount_ = 0;
		return;
	}

	for (uint32_t i = 0; i < gridDrawCount_; ++i) {

		// スナップグリッド描画、固定間隔とシーン深度を渡してメッシュに隠れるようにする
		gridRenderer_->Render(graphicsCore, *camera, surface, gridMinorStep_, snapGridOcclusionDepth_);
	}
	gridDrawCount_ = 0;
	gridMinorStep_ = 0.0f;
	snapGridOcclusionDepth_ = nullptr;
}
