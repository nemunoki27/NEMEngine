#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace {

	bool IsPointInsideProjectedRect(const Engine::Vector2& point,
		const Engine::Vector2& rectMin, const Engine::Vector2& rectMax,
		const Engine::Matrix4x4& worldViewProjection,
		uint32_t viewWidth, uint32_t viewHeight) {

		const std::array<Engine::Vector3, 4> corners{
			Engine::Vector3(rectMin.x, rectMin.y, 0.0f),
			Engine::Vector3(rectMax.x, rectMin.y, 0.0f),
			Engine::Vector3(rectMin.x, rectMax.y, 0.0f),
			Engine::Vector3(rectMax.x, rectMax.y, 0.0f),
		};
		Engine::Vector2 projectedMin = Engine::Vector2::AnyInit(
			(std::numeric_limits<float>::max)());
		Engine::Vector2 projectedMax = Engine::Vector2::AnyInit(
			-(std::numeric_limits<float>::max)());
		for (const Engine::Vector3& corner : corners) {
			const Engine::Vector3 ndc = Engine::Vector3::Transform(
				corner, worldViewProjection);
			const Engine::Vector2 pixel(
				(ndc.x + 1.0f) * 0.5f * static_cast<float>(viewWidth),
				(1.0f - ndc.y) * 0.5f * static_cast<float>(viewHeight));
			projectedMin.x = (std::min)(projectedMin.x, pixel.x);
			projectedMin.y = (std::min)(projectedMin.y, pixel.y);
			projectedMax.x = (std::max)(projectedMax.x, pixel.x);
			projectedMax.y = (std::max)(projectedMax.y, pixel.y);
		}
		return point.x >= projectedMin.x && point.x <= projectedMax.x &&
			point.y >= projectedMin.y && point.y <= projectedMax.y;
	}
}

//============================================================================
//	EditorManager picking methods
//============================================================================
Engine::Entity Engine::EditorManager::Execute2DPick(const Vector2& inputPixel, const ResolvedRenderView& view, ECSWorld* world) {

	if (!world) {
		return Entity::Null();
	}

	if (view.width == 0 || view.height == 0) {
		return Entity::Null();
	}

	struct HitRecord {
		Entity entity;
		int32_t layer;
		int32_t order;
	};
	std::vector<HitRecord> hits;

	world->ForEach<SpriteRendererComponent>([&](const Entity& entity, const SpriteRendererComponent& renderer) {
		if (!IsScenePickDimensionAllowed(*world, entity, editorState_.sceneViewPickDimension)) {
			return;
		}
		if (!RenderItemExtract::IsVisible(*world, entity, renderer.visible)) {
			return;
		}

		const UIElementRuntime* uiRuntime =
			UIRuntimeService::GetInstance().Find(*world, entity);
		const RenderCameraDomain cameraDomain = uiRuntime ?
			RenderCameraDomain::Screen : RenderCameraDomain::Orthographic;
		const ResolvedCameraView* camera = view.FindCamera(cameraDomain);
		if (!camera) {
			return;
		}

		// スプライトの矩形領域内か判定
		const Vector2 rectMin(
			-renderer.pivot.x * renderer.size.x,
			-renderer.pivot.y * renderer.size.y);
		const Vector2 rectMax(
			(1.0f - renderer.pivot.x) * renderer.size.x,
			(1.0f - renderer.pivot.y) * renderer.size.y);
		const Matrix4x4 worldMatrix = uiRuntime ?
			uiRuntime->screenMatrix : RenderItemExtract::GetWorldMatrix(*world, entity);
		if (IsPointInsideProjectedRect(inputPixel, rectMin, rectMax,
			worldMatrix * camera->matrices.viewProjectionMatrix,
			view.width, view.height)) {
			hits.push_back({ entity, renderer.layer, renderer.order });
		}
	});

	world->ForEach<TextRendererComponent>([&](const Entity& entity, const TextRendererComponent& renderer) {
		if (!IsScenePickDimensionAllowed(*world, entity, editorState_.sceneViewPickDimension)) {
			return;
		}
		if (!RenderItemExtract::IsVisible(*world, entity, renderer.visible)) {
			return;
		}
		const UIElementRuntime* uiRuntime =
			UIRuntimeService::GetInstance().Find(*world, entity);
		if (!uiRuntime && renderer.dimension != Dimension::Type2D) {
			return;
		}
		const TextLayoutRuntimeComponent* layout =
			world->TryGetComponent<TextLayoutRuntimeComponent>(entity);
		const std::span<const TextLayoutGlyph> glyphs =
			GetTextLayoutGlyphs(*world, entity);
		if (!layout || !layout->valid || glyphs.empty()) {
			return;
		}

		const RenderCameraDomain cameraDomain = uiRuntime ?
			RenderCameraDomain::Screen : RenderCameraDomain::Orthographic;
		const ResolvedCameraView* camera = view.FindCamera(cameraDomain);
		if (!camera) {
			return;
		}

		const Matrix4x4 worldMatrix = uiRuntime ?
			uiRuntime->screenMatrix : RenderItemExtract::GetWorldMatrix(*world, entity);
		const std::span<const TextCharTransform> charTransforms =
			GetTextCharTransforms(*world, entity);
		for (size_t glyphIndex = 0; glyphIndex < glyphs.size(); ++glyphIndex) {
			const TextCharTransform* charTransform = glyphIndex < charTransforms.size() ?
				&charTransforms[glyphIndex] : nullptr;
			const TextGlyphGeometry geometry = ResolveTextGlyphGeometry(
				renderer, *layout, glyphs[glyphIndex], charTransform, worldMatrix);
			if (IsPointInsideProjectedRect(inputPixel,
				geometry.rectMin, geometry.rectMax,
				geometry.worldMatrix * camera->matrices.viewProjectionMatrix,
				view.width, view.height)) {
				hits.push_back({ entity, renderer.layer, renderer.order });
				break;
			}
		}
	});

	if (hits.empty()) {
		return Entity::Null();
	}

	// レイヤーとオーダーの降順でソートし手前にあるものを優先する
	std::sort(hits.begin(), hits.end(), [](const HitRecord& a, const HitRecord& b) {
		if (a.layer != b.layer) return a.layer > b.layer;
		return a.order > b.order;
	});

	return hits.front().entity;
}

void Engine::EditorManager::ExecuteSceneMeshPicking(GraphicsCore& graphicsCore,
	[[maybe_unused]] const EditorContext& context, RenderPipelineRunner& renderPipeline) {

	// 以下の条件のいずれかを満たす場合はピック処理を行わない
	if (!initialized_ || layoutState_.hidePanels || !editorState_.enableScenePick) {
		return;
	}

	Input* input = Input::GetInstance();

	// 押下を開始した同じViewでドラッグせず離した場合だけ選択を確定する
	if (input->ReleaseMouse(MouseButton::Left) &&
		editorState_.scenePickPressActive) {

		const bool overPressedViewport =
			editorState_.scenePickPressGameView ?
			editorState_.gameViewportHovered :
			editorState_.sceneViewportHovered;
		editorState_.scenePickPressActive = false;
		if (overPressedViewport &&
			!ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) {

			editorState_.scenePickClickPending = true;
			editorState_.scenePickClickAdditive =
				ImGui::IsKeyDown(ImGuiKey_LeftShift);
			// GPU結果が先に解決済みなら、このリリースで確定する
			if (editorState_.scenePickCandidateRequestID ==
				editorState_.scenePickRequestID &&
				context.activeWorld) {
				editorState_.CommitScenePick(
					*context.activeWorld);
			}
		} else {

			editorState_.scenePickClickPending = false;
		}
	}

	auto executePick = [&](InputViewArea inputArea, RenderViewKind viewKind) {

			// ビューの上で左クリックされたフレームのみ処理する
			if (!input->HasViewRect(inputArea)) {
				return false;
			}
			if (!input->IsMouseOnView(inputArea)) {
				return false;
			}
			// ビューのImageが最前面でホバーされている時だけ反応する
			// 他のImGuiウィンドウやポップアップが上にある状態での誤選択を防ぐ
			const bool viewportHovered = (viewKind == RenderViewKind::Game) ?
				editorState_.gameViewportHovered : editorState_.sceneViewportHovered;
			if (!viewportHovered) {
				return false;
			}
			if (!input->TriggerMouse(MouseButton::Left)) {
				return false;
			}
			// 押すたびに要求IDを進め、古いGPU結果をこのクリックへ適用しない
			++editorState_.scenePickRequestID;
			if (editorState_.scenePickRequestID == 0) {
				++editorState_.scenePickRequestID;
			}
			editorState_.scenePickDragEntity = Entity::Null();
			editorState_.scenePickCandidateSubMesh = 0;
			editorState_.scenePickCandidateSubMeshID = UUID{};
			editorState_.scenePickCandidateRequestID = 0;
			editorState_.scenePickClickPending = false;
			editorState_.scenePickPressActive = true;
			editorState_.scenePickPressGameView =
				viewKind == RenderViewKind::Game;

			// マウス座標を取得
			const std::optional<Vector2> mousePosInView = input->GetMousePosInView(inputArea);
			if (!mousePosInView.has_value()) {
				return false;
			}

			// 即時ピック(2D/Overlay)はここで候補だけ設定し、選択はリリース時に確定する
			auto selectHit = [&](const Entity& hit) {
				editorState_.scenePickDragEntity = hit;
				editorState_.scenePickCandidateSubMesh = 0;
				editorState_.scenePickCandidateSubMeshID = UUID{};
				editorState_.scenePickCandidateRequestID =
					editorState_.scenePickRequestID;
				};

			// SceneView専用Overlayは通常2D/TLASより優先してEntity単位で選択する
			if (viewKind == RenderViewKind::Scene) {
				Entity overlayHit = Entity::Null();
				if (sceneComponentOverlayPicker_.Pick(context.activeWorld,
					renderPipeline.GetResolvedView(viewKind), mousePosInView.value(), overlayHit) &&
					context.activeWorld && IsScenePickDimensionAllowed(*context.activeWorld, overlayHit,
						editorState_.sceneViewPickDimension)) {
					selectHit(overlayHit);
					return true;
				}
			}

			// 2Dエンティティのピック処理を優先実行
			Entity hitEntity2D = Execute2DPick(mousePosInView.value(), renderPipeline.GetResolvedView(viewKind), context.activeWorld);
			if (hitEntity2D.IsValid()) {

				// 2Dが優先されるため、GPUによる3Dピックは行わず、即座に選択を確定する
				selectHit(hitEntity2D);
				return true;
			}

			// 1x1整数RTへクリック画素だけを描画しreadbackを予約する
			MultiRenderTarget* pickTarget =
				meshSubMeshPicker_->GetRenderTarget();
			std::optional<Dimension> dimensionFilter{};
			if (editorState_.sceneViewPickDimension != SceneViewPickDimension::Both) {
				dimensionFilter = ResolveSceneViewCameraDimension(editorState_.sceneViewPickDimension);
			}
			if (pickTarget && renderPipeline.RenderMeshPicking(graphicsCore, viewKind,
				mousePosInView.value(), *pickTarget, dimensionFilter)) {

				if (meshSubMeshPicker_->ExecuteReadback(
					graphicsCore, editorState_.scenePickRequestID)) {
					return true;
				}
			}
			// 描画またはreadbackを開始できなかった場合も空候補としてリリース可能にする
			editorState_.scenePickCandidateRequestID =
				editorState_.scenePickRequestID;
			return true;
		};

	// SceneViewは従来通り、ギズモ操作中はピックしない
	if (layoutState_.showSceneView && !editorState_.useSceneGizmo) {
		if (executePick(InputViewArea::Scene, RenderViewKind::Scene)) {
			return;
		}
	}

	// GameViewにもSceneViewと同じTLASピックだけを通し、マニピュレーターは表示しない
	if (layoutState_.showGameView) {
		executePick(InputViewArea::Game, RenderViewKind::Game);
	}
}
