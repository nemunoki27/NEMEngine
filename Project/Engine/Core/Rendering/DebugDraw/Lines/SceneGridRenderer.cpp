#include "SceneGridRenderer.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Pipelines/BuiltinShaderSource.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

//============================================================================
//	SceneGridRenderer classMethods
//============================================================================
namespace {

	struct GridPoint2D {
		float x = 0.0f;
		float z = 0.0f;
	};

	struct ClipSpacePosition {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;
	};

	constexpr float kEpsilon = 1e-5f;
	constexpr float kPointMergeEpsilon = 1e-3f;

	// 1 / 2 / 5 * 10^nにスナップ
	float SnapGridStep(float rawStep) {

		rawStep = (std::max)(rawStep, 0.001f);

		float exponent = std::floor(std::log10(rawStep));
		float base = std::pow(10.0f, exponent);
		float normalized = rawStep / base;

		float snapped = 1.0f;
		if (normalized < 1.5f) {
			snapped = 1.0f;
		} else if (normalized < 3.5f) {
			snapped = 2.0f;
		} else if (normalized < 7.5f) {
			snapped = 5.0f;
		} else {
			snapped = 10.0f;
		}

		return snapped * base;
	}

	float NextGridStep(float step) {
		return SnapGridStep(step * 1.9f);
	}

	float PrevGridStep(float step) {
		return SnapGridStep(step / 1.9f);
	}

	Engine::Vector3 UnprojectNDC(const Engine::Matrix4x4& invViewProj, float x, float y, float z) {

		return Engine::Vector3::Transform(Engine::Vector3(x, y, z), invViewProj);
	}

	ClipSpacePosition TransformToClip(const Engine::Vector3& point, const Engine::Matrix4x4& matrix) {

		ClipSpacePosition clip{};
		clip.x = point.x * matrix.m[0][0] + point.y * matrix.m[1][0] + point.z * matrix.m[2][0] + matrix.m[3][0];
		clip.y = point.x * matrix.m[0][1] + point.y * matrix.m[1][1] + point.z * matrix.m[2][1] + matrix.m[3][1];
		clip.z = point.x * matrix.m[0][2] + point.y * matrix.m[1][2] + point.z * matrix.m[2][2] + matrix.m[3][2];
		clip.w = point.x * matrix.m[0][3] + point.y * matrix.m[1][3] + point.z * matrix.m[2][3] + matrix.m[3][3];
		return clip;
	}

	bool ProjectWorldToScreen(const Engine::Vector3& point, const Engine::Matrix4x4& viewProjection,
		uint32_t width, uint32_t height, Engine::Vector2& outScreen) {

		ClipSpacePosition clip = TransformToClip(point, viewProjection);
		if (std::abs(clip.w) < kEpsilon) {
			return false;
		}

		float invW = 1.0f / clip.w;
		float ndcX = clip.x * invW;
		float ndcY = clip.y * invW;

		outScreen.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
		outScreen.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height);
		return true;
	}

	bool AddUniquePoint(std::vector<GridPoint2D>& polygon, const GridPoint2D& point) {

		for (const auto& p : polygon) {
			float dx = p.x - point.x;
			float dz = p.z - point.z;
			if ((dx * dx + dz * dz) <= (kPointMergeEpsilon * kPointMergeEpsilon)) {
				return false;
			}
		}

		polygon.push_back(point);
		return true;
	}

	void SortPolygon(std::vector<GridPoint2D>& polygon) {

		if (polygon.size() < 3) {
			return;
		}

		float cx = 0.0f;
		float cz = 0.0f;
		for (const auto& p : polygon) {
			cx += p.x;
			cz += p.z;
		}
		cx /= static_cast<float>(polygon.size());
		cz /= static_cast<float>(polygon.size());

		std::sort(polygon.begin(), polygon.end(),
			[cx, cz](const GridPoint2D& a, const GridPoint2D& b) {
				float aa = std::atan2(a.z - cz, a.x - cx);
				float ab = std::atan2(b.z - cz, b.x - cx);
				return aa < ab;
			});
	}

	bool IntersectGroundRay(const Engine::Vector3& cameraPos,
		const Engine::Vector3& nearP, const Engine::Vector3& farP,
		float planeY, float maxGroundRayDistance,
		GridPoint2D& outPoint) {

		Engine::Vector3 direction = farP - nearP;
		float length = direction.Length();
		if (length < kEpsilon) {
			return false;
		}
		direction /= length;

		if (std::abs(direction.y) < kEpsilon) {
			return false;
		}

		float t = (planeY - cameraPos.y) / direction.y;
		if (t <= 0.0f) {
			return false;
		}

		Engine::Vector3 hit = cameraPos + direction * t;

		// 浅い角度で極端に遠くへ飛ぶのを抑える
		Engine::Vector3 horizontal = hit - cameraPos;
		horizontal.y = 0.0f;
		float horizontalDistance = horizontal.Length();
		if (maxGroundRayDistance < horizontalDistance) {
			horizontal /= horizontalDistance;
			hit = cameraPos + horizontal * maxGroundRayDistance;
			hit.y = planeY;
		}

		outPoint.x = hit.x;
		outPoint.z = hit.z;
		return true;
	}

	bool TryIntersectGroundFromNDC(const Engine::ResolvedCameraView& camera,
		const Engine::Matrix4x4& invViewProj,
		float ndcX, float ndcY,
		float planeY, float maxGroundRayDistance,
		Engine::Vector3& outPoint) {

		Engine::Vector3 nearP = UnprojectNDC(invViewProj, ndcX, ndcY, 0.0f);
		Engine::Vector3 farP = UnprojectNDC(invViewProj, ndcX, ndcY, 1.0f);

		GridPoint2D hit2D{};
		if (!IntersectGroundRay(camera.cameraPos, nearP, farP, planeY, maxGroundRayDistance, hit2D)) {
			return false;
		}

		outPoint = Engine::Vector3(hit2D.x, planeY, hit2D.z);
		return true;
	}

	bool BuildVisibleGroundPolygon(const Engine::ResolvedCameraView& camera,
		int samplesPerEdge,
		float planeY,
		float maxGroundRayDistance,
		std::vector<GridPoint2D>& outPolygon) {

		outPolygon.clear();

		const Engine::Matrix4x4 invVP = camera.matrices.inverseProjectionMatrix * camera.matrices.inverseViewMatrix;
		const int sampleCount = (std::max)(samplesPerEdge, 4);

		auto addSample = [&](float ndcX, float ndcY) {
			Engine::Vector3 nearP = UnprojectNDC(invVP, ndcX, ndcY, 0.0f);
			Engine::Vector3 farP = UnprojectNDC(invVP, ndcX, ndcY, 1.0f);

			GridPoint2D hit{};
			if (IntersectGroundRay(camera.cameraPos, nearP, farP, planeY, maxGroundRayDistance, hit)) {
				AddUniquePoint(outPolygon, hit);
			}
			};

		// 下辺
		for (int i = 0; i < sampleCount; ++i) {
			float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
			addSample(Math::Lerp(-1.0f, 1.0f, t), -1.0f);
		}
		// 右辺
		for (int i = 1; i < sampleCount - 1; ++i) {
			float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
			addSample(1.0f, Math::Lerp(-1.0f, 1.0f, t));
		}
		// 上辺
		for (int i = sampleCount - 1; i >= 0; --i) {
			float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
			addSample(Math::Lerp(-1.0f, 1.0f, t), 1.0f);
		}
		// 左辺
		for (int i = sampleCount - 2; i >= 1; --i) {
			float t = static_cast<float>(i) / static_cast<float>(sampleCount - 1);
			addSample(-1.0f, Math::Lerp(-1.0f, 1.0f, t));
		}

		if (outPolygon.size() < 3) {
			return false;
		}

		SortPolygon(outPolygon);
		return true;
	}

	float DistanceXZ(const Engine::Vector3& a, const Engine::Vector3& b) {

		float dx = a.x - b.x;
		float dz = a.z - b.z;
		return std::sqrt(dx * dx + dz * dz);
	}

	bool EstimateProjectedStepPixels(const Engine::ResolvedCameraView& camera,
		uint32_t width, uint32_t height,
		const Engine::Vector3& anchor, float worldStep,
		float& outPixels) {

		Engine::Vector2 screenAnchor{};
		if (!ProjectWorldToScreen(anchor, camera.matrices.viewProjectionMatrix, width, height, screenAnchor)) {
			return false;
		}

		float bestPixels = 0.0f;
		bool success = false;

		const std::array<Engine::Vector3, 2> offsets = {
			Engine::Vector3(worldStep, 0.0f, 0.0f),
			Engine::Vector3(0.0f, 0.0f, worldStep),
		};

		for (const auto& offset : offsets) {
			Engine::Vector2 screenOther{};
			if (!ProjectWorldToScreen(anchor + offset, camera.matrices.viewProjectionMatrix, width, height, screenOther)) {
				continue;
			}

			float dx = screenOther.x - screenAnchor.x;
			float dy = screenOther.y - screenAnchor.y;
			float pixels = std::sqrt(dx * dx + dy * dy);
			bestPixels = (std::max)(bestPixels, pixels);
			success = true;
		}

		outPixels = bestPixels;
		return success;
	}

	Engine::Vector3 ChooseGridAnchor(const Engine::ResolvedCameraView& camera,
		const std::vector<GridPoint2D>& polygon,
		float planeY,
		float maxGroundRayDistance) {

		const Engine::Matrix4x4 invVP = camera.matrices.inverseProjectionMatrix * camera.matrices.inverseViewMatrix;

		const std::array<Engine::Vector2, 6> samples = {
			Engine::Vector2(0.0f, -0.80f),
			Engine::Vector2(0.0f, -0.60f),
			Engine::Vector2(0.0f, -0.40f),
			Engine::Vector2(-0.35f, -0.75f),
			Engine::Vector2(0.35f, -0.75f),
			Engine::Vector2(0.0f,  0.00f),
		};

		for (const auto& sample : samples) {
			Engine::Vector3 point{};
			if (TryIntersectGroundFromNDC(camera, invVP, sample.x, sample.y, planeY, maxGroundRayDistance, point)) {
				return point;
			}
		}

		if (!polygon.empty()) {
			float bestDistance = (std::numeric_limits<float>::max)();
			Engine::Vector3 bestPoint(polygon.front().x, planeY, polygon.front().z);
			for (const auto& p : polygon) {
				Engine::Vector3 candidate(p.x, planeY, p.z);
				float distance = DistanceXZ(candidate, camera.cameraPos);
				if (distance < bestDistance) {
					bestDistance = distance;
					bestPoint = candidate;
				}
			}
			return bestPoint;
		}

		return Engine::Vector3(camera.cameraPos.x, planeY, camera.cameraPos.z);
	}

	struct GridStepBlend {
		float minorStep0 = 1.0f;
		float minorStep1 = 1.0f;
		float blend = 0.0f;
	};

	GridStepBlend DetermineMinorStepBlend(const Engine::ResolvedCameraView& camera,
		uint32_t width, uint32_t height,
		const std::vector<GridPoint2D>& polygon,
		float planeY,
		float maxGroundRayDistance,
		float baseHeightDivisor,
		float baseMinStep,
		float targetPixelMin,
		float targetPixelMax) {

		Engine::Vector3 anchor = ChooseGridAnchor(camera, polygon, planeY, maxGroundRayDistance);

		float safeBaseHeightDivisor = (std::max)(baseHeightDivisor, 0.001f);
		float safeBaseMinStep = (std::max)(baseMinStep, 0.001f);
		float safeTargetPixelMin = (std::max)(targetPixelMin, 1.0f);
		float safeTargetPixelMax = (std::max)(targetPixelMax, safeTargetPixelMin + 1.0f);

		// 1ワールド単位が今の画面で何ピクセルか
		float pixelsPerUnit = 0.0f;
		if (!EstimateProjectedStepPixels(camera, width, height, anchor, 1.0f, pixelsPerUnit) || pixelsPerUnit <= kEpsilon) {

			float fallback = SnapGridStep(
				(std::max)(std::abs(camera.cameraPos.y - planeY) / safeBaseHeightDivisor, safeBaseMinStep));

			GridStepBlend result{};
			result.minorStep0 = fallback;
			result.minorStep1 = NextGridStep(fallback);
			result.blend = 0.0f;
			return result;
		}

		// target rangeの真ん中ではなく、対数的な中心を使う
		// 64～256なら128付近を狙う
		float targetPixelCenter = std::sqrt(safeTargetPixelMin * safeTargetPixelMax);

		// 理想的な連続値のminor step
		float idealMinorStep = (std::max)(targetPixelCenter / pixelsPerUnit, safeBaseMinStep);

		// idealMinorStepを挟む2つのsnapped stepを求める
		float upper = SnapGridStep(idealMinorStep);
		if (upper < idealMinorStep) {
			upper = NextGridStep(upper);
		}

		float lower = PrevGridStep(upper);
		lower = (std::max)(lower, safeBaseMinStep);

		// lower == upperの場合はそのまま
		if (std::abs(upper - lower) < kEpsilon) {
			GridStepBlend result{};
			result.minorStep0 = lower;
			result.minorStep1 = upper;
			result.blend = 0.0f;
			return result;
		}

		// 対数空間で補間すると1/2/5ステップでも自然
		float denom = std::log(upper / lower);
		float blend = 0.0f;
		if (std::abs(denom) > kEpsilon) {
			blend = std::log(idealMinorStep / lower) / denom;
		}
		blend = Math::Saturate(blend);

		// cubic smooth
		blend = blend * blend * (3.0f - 2.0f * blend);

		GridStepBlend result{};
		result.minorStep0 = lower;
		result.minorStep1 = upper;
		result.blend = blend;
		return result;
	}
}

Engine::SceneGridRenderer::~SceneGridRenderer() {

	// フレーム内複数描画用に保持した定数バッファを終了時に明示resetする
	for (auto& frameBuffers : passBuffers_) {
		for (auto& buffer : frameBuffers) {
			buffer.reset();
		}
		frameBuffers.clear();
	}
}

void Engine::SceneGridRenderer::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
	DxShaderCompiler* compiler = graphicsCore.GetDXObject().GetDxShaderCompiler();

	GraphicsPipelineDesc desc{};
	desc.type = PipelineType::Vertex;

	desc.preRaster.file = BuiltinShaderSource::Line::AnalyticGridVS;
	desc.preRaster.entry = "main";
	desc.preRaster.profile = "vs_6_0";

	desc.pixel.file = BuiltinShaderSource::Line::AnalyticGridPS;
	desc.pixel.entry = "main";
	desc.pixel.profile = "ps_6_0";

	desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;

	desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	desc.depthStencil.DepthEnable = TRUE;
	desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.depthStencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	desc.depthStencil.StencilEnable = FALSE;

	desc.sampleDesc = { 1, 0 };
	desc.topologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	desc.numRenderTargets = 1;
	desc.rtvFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
	desc.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	bool created = pipeline_.CreateGraphics(device, compiler, desc);
	Assert::Call(created, "SceneGridRenderer analytic grid pipeline create failed");

	for (auto& buffers : passBuffers_) {
		buffers.reserve(4);
	}

	initialized_ = true;
}

void Engine::SceneGridRenderer::BeginFrame() {

	passBufferIndices_[GraphicsFrameState::GetCurrentIndex()] = 0;
}

Engine::SceneGridRenderer::GridPassConstants Engine::SceneGridRenderer::BuildPassConstants(
	const ResolvedCameraView& camera, uint32_t width, uint32_t height, float fixedMinorStep) const {

	GridPassConstants constants{};

	std::vector<GridPoint2D> polygon{};
	bool hasPolygon = BuildVisibleGroundPolygon(
		camera,
		gridVisiblePolygonSamplesPerEdge_,
		gridPlaneY_,
		gridMaxGroundRayDistance_,
		polygon);

	// fixedMinorStep指定時は自動フィットせず固定間隔にする、未指定時は従来通りカメラ距離で自動調整する
	float minorStep0;
	float minorStep1;
	float stepBlendValue;
	if (fixedMinorStep > 0.0f) {

		minorStep0 = fixedMinorStep;
		minorStep1 = fixedMinorStep;
		stepBlendValue = 0.0f;
	} else {

		const GridStepBlend stepBlend = DetermineMinorStepBlend(
			camera,
			width,
			height,
			polygon,
			gridPlaneY_,
			gridMaxGroundRayDistance_,
			gridMinorBaseHeightDivisor_,
			gridMinorBaseMinStep_,
			gridMinorTargetPixelMin_,
			gridMinorTargetPixelMax_);
		minorStep0 = stepBlend.minorStep0;
		minorStep1 = stepBlend.minorStep1;
		stepBlendValue = stepBlend.blend;
	}

	float majorStep0 = minorStep0 * 10.0f;
	float coarseStep0 = majorStep0 * 10.0f;

	float majorStep1 = minorStep1 * 10.0f;
	float coarseStep1 = majorStep1 * 10.0f;

	const float maxGridRadius = std::clamp(
		(std::max)(coarseStep0, coarseStep1) * gridRadiusCoarseStepRate_,
		gridRadiusMin_,
		gridRadiusMax_);

	float visibleRadius = maxGridRadius;
	if (hasPolygon) {
		visibleRadius = 1.0f;
		for (const auto& p : polygon) {
			Vector3 point(p.x, gridPlaneY_, p.z);
			visibleRadius = (std::max)(visibleRadius, DistanceXZ(point, camera.cameraPos));
		}
		visibleRadius = (std::min)(visibleRadius, maxGridRadius);
	}

	constants.stepData0 = Vector4(
		minorStep0,
		majorStep0,
		coarseStep0,
		visibleRadius);

	constants.stepData1 = Vector4(
		minorStep1,
		majorStep1,
		coarseStep1,
		stepBlendValue);

	const auto makeFadeStartDistance = [&](float rate) {
		return visibleRadius * std::clamp(rate, 0.0f, 10.0f);
		};

	const auto makeFadeEndDistance = [&](float startRate, float endRate) {
		float startDistance = makeFadeStartDistance(startRate);
		float endDistance = visibleRadius * (std::max)(endRate, startRate + 0.001f);
		return (std::max)(endDistance, startDistance + 1.0f);
		};

	constants.inverseViewProjectionMatrix = camera.matrices.inverseProjectionMatrix * camera.matrices.inverseViewMatrix;
	constants.viewProjectionMatrix = camera.matrices.viewProjectionMatrix;

	constants.cameraPositionAndPlaneY = Vector4(
		camera.cameraPos.x,
		camera.cameraPos.y,
		camera.cameraPos.z,
		gridPlaneY_);

	constants.viewportSize = Vector4(
		static_cast<float>(width),
		static_cast<float>(height),
		0.0f,
		0.0f);

	constants.thicknessFadeAndHorizon = Vector4(
		gridThicknessFadePower_,
		gridMinHalfThickness_,
		gridHorizonFadeStart_,
		gridHorizonFadeEnd_);

	// スナップ用の等間隔グリッドはMinorを不透明で描く、通常グリッドは設定値のまま
	const float minorAlpha = (fixedMinorStep > 0.0f) ? 1.0f : gridMinorBaseAlpha_;
	constants.minorColor = Color4(1.0f, 1.0f, 1.0f, minorAlpha);
	constants.minorParams0 = Vector4(
		gridMinorLineThickness_,
		gridMinorFarThicknessRate_,
		makeFadeStartDistance(gridMinorFadeStartRate_),
		makeFadeEndDistance(gridMinorFadeStartRate_, gridMinorFadeEndRate_));
	constants.minorParams1 = Vector4(
		gridMinorFadePower_,
		0.0f, 0.0f, 0.0f);

	constants.majorColor = Color4(1.0f, 1.0f, 1.0f, gridMajorBaseAlpha_);
	constants.majorParams0 = Vector4(
		gridMajorLineThickness_,
		gridMajorFarThicknessRate_,
		makeFadeStartDistance(gridMajorFadeStartRate_),
		makeFadeEndDistance(gridMajorFadeStartRate_, gridMajorFadeEndRate_));
	constants.majorParams1 = Vector4(
		gridMajorFadePower_,
		0.0f, 0.0f, 0.0f);

	constants.coarseColor = Color4(1.0f, 1.0f, 1.0f, gridCoarseBaseAlpha_);
	constants.coarseParams0 = Vector4(
		gridCoarseLineThickness_,
		gridCoarseFarThicknessRate_,
		makeFadeStartDistance(gridCoarseFadeStartRate_),
		makeFadeEndDistance(gridCoarseFadeStartRate_, gridCoarseFadeEndRate_));
	constants.coarseParams1 = Vector4(
		gridCoarseFadePower_,
		0.0f, 0.0f, 0.0f);

	constants.axisXColor = gridAxisXLineColor_;
	constants.axisZColor = gridAxisZLineColor_;
	constants.axisParams = Vector4(
		gridAxisLineThickness_,
		0.0f,
		0.0f,
		visibleRadius);

	return constants;
}

Engine::DxConstBuffer<Engine::SceneGridRenderer::GridPassConstants>& Engine::SceneGridRenderer::AllocatePassBuffer(
	GraphicsCore& graphicsCore) {

	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	auto& buffers = passBuffers_[frameIndex];
	uint32_t& bufferIndex = passBufferIndices_[frameIndex];
	if (buffers.size() <= bufferIndex) {

		// 同じフレーム内で複数のカメラから描画されても、記録済みコマンドの定数を上書きしない
		auto buffer = std::make_unique<DxConstBuffer<GridPassConstants>>();
		buffer->CreateBuffer(graphicsCore.GetDXObject().GetDevice());
		buffers.emplace_back(std::move(buffer));
	}
	return *buffers[bufferIndex++];
}

void Engine::SceneGridRenderer::Render(GraphicsCore& graphicsCore,
	const ResolvedCameraView& camera, MultiRenderTarget& surface, float fixedMinorStep, DepthTexture2D* occlusionDepth) {

	if (!initialized_) {
		return;
	}
	if (!camera.valid) {
		return;
	}
	if (surface.GetWidth() == 0 || surface.GetHeight() == 0) {
		return;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	surface.TransitionForRender(*dxCommand);
	if (RenderTexture2D* color = surface.GetColorTexture(0)) {

		if (occlusionDepth) {

			// シーン深度でテストして線をメッシュに隠す、深度書き込みはZEROなので内容は壊さない
			occlusionDepth->Transition(*dxCommand, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			dxCommand->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()),
				occlusionDepth->GetDSVCPUHandle());
		} else if (DepthTexture2D* depth = surface.GetDepthTexture()) {

			dxCommand->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()),
				depth->GetDSVCPUHandle());
		} else {

			dxCommand->BindRenderTargets(std::optional<RenderTarget>(color->GetRenderTarget()), std::nullopt);
		}
		dxCommand->SetViewportAndScissor(surface.GetWidth(), surface.GetHeight());
	} else {

		return;
	}

	GridPassConstants constants = BuildPassConstants(camera, surface.GetWidth(), surface.GetHeight(), fixedMinorStep);
	DxConstBuffer<GridPassConstants>& passBuffer = AllocatePassBuffer(graphicsCore);
	passBuffer.TransferData(constants);

	commandList->SetGraphicsRootSignature(pipeline_.GetRootSignature());
	commandList->SetPipelineState(pipeline_.GetGraphicsPipeline(BlendMode::Normal));

	// パイプラインが変わった時だけスロットを再解決する
	gridBindCache_.Sync(pipeline_);
	if (gridBindCache_.Has(gridCBVSlot_)) {
		RootBindingCommand::SetGraphicsCBV(commandList, gridBindCache_.Get(gridCBVSlot_),
			passBuffer.GetResource()->GetGPUVirtualAddress());
	}

	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}
