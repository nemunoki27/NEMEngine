#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/Platform/Input/InputSystem.h>

// c++
#include <algorithm>
#include <limits>
#include <optional>

//============================================================================
//	EditorManager picking methods
//============================================================================
Engine::Entity Engine::EditorManager::Execute2DPick(const Vector2& inputPixel, const ResolvedRenderView& view, ECSWorld* world) {

	if (!world) {
		return Entity::Null();
	}

	const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Orthographic);
	if (!camera) {
		return Entity::Null();
	}

	// NDC座標への変換
	// inputPixelはGetMousePosInView()により、描画元(view.width, view.height)の解像度にスケーリングされた座標
	float ndcX = (inputPixel.x / static_cast<float>(view.width)) * 2.0f - 1.0f;
	float ndcY = 1.0f - (inputPixel.y / static_cast<float>(view.height)) * 2.0f;

	Vector3 ndcOrigin(ndcX, ndcY, 0.0f);
	Vector3 ndcTarget(ndcX, ndcY, 1.0f);

	struct HitRecord {
		Entity entity;
		int32_t layer;
		int32_t order;
	};
	std::vector<HitRecord> hits;

	world->ForEach<SpriteRendererComponent>([&](const Entity& entity, const SpriteRendererComponent& renderer) {
		if (!RenderItemExtract::IsVisible(*world, entity, renderer.visible)) {
			return;
		}

		Matrix4x4 worldMatrix = RenderItemExtract::GetWorldMatrix(*world, entity);
		Matrix4x4 wvp = worldMatrix * camera->matrices.viewProjectionMatrix;
		Matrix4x4 wvpInv = Matrix4x4::Inverse(wvp);

		// NDCからローカル空間へのレイを計算
		Vector3 localOrigin = Vector3::Transform(ndcOrigin, wvpInv);
		Vector3 localTarget = Vector3::Transform(ndcTarget, wvpInv);
		Vector3 localDir = Vector3::Normalize(localTarget - localOrigin);

		// Z=0平面との交差判定(rd.zが0に近い場合は平行なのでスキップ)
		if (std::abs(localDir.z) < 1e-5f) {
			return;
		}

		float t = -localOrigin.z / localDir.z;
		// 後ろにあるものはピッキングしない
		if (t < 0.0f) {
			return;
		}

		Vector3 hitPoint = localOrigin + localDir * t;

		// スプライトの矩形領域内か判定
		float minX = -renderer.pivot.x * renderer.size.x;
		float maxX = (1.0f - renderer.pivot.x) * renderer.size.x;
		float minY = -renderer.pivot.y * renderer.size.y;
		float maxY = (1.0f - renderer.pivot.y) * renderer.size.y;

		if (hitPoint.x >= minX && hitPoint.x <= maxX &&
			hitPoint.y >= minY && hitPoint.y <= maxY) {
			hits.push_back({ entity, renderer.layer, renderer.order });
		}
	});

	world->ForEach<TextRendererComponent>([&](const Entity& entity, const TextRendererComponent& renderer) {
		if (!RenderItemExtract::IsVisible(*world, entity, renderer.visible)) {
			return;
		}
		if (!renderer.runtimeLayout.valid || renderer.runtimeLayout.glyphs.empty()) {
			return;
		}

		Matrix4x4 worldMatrix = RenderItemExtract::GetWorldMatrix(*world, entity);
		Matrix4x4 wvp = worldMatrix * camera->matrices.viewProjectionMatrix;
		Matrix4x4 wvpInv = Matrix4x4::Inverse(wvp);

		// NDCからローカル空間へのレイを計算
		Vector3 localOrigin = Vector3::Transform(ndcOrigin, wvpInv);
		Vector3 localTarget = Vector3::Transform(ndcTarget, wvpInv);
		Vector3 localDir = Vector3::Normalize(localTarget - localOrigin);

		if (std::abs(localDir.z) < 1e-5f) {
			return;
		}

		float t = -localOrigin.z / localDir.z;
		if (t < 0.0f) {
			return;
		}

		Vector3 hitPoint = localOrigin + localDir * t;

		// テキストの全体の矩形を計算
		float minX = (std::numeric_limits<float>::max)();
		float maxX = -(std::numeric_limits<float>::max)();
		float minY = (std::numeric_limits<float>::max)();
		float maxY = -(std::numeric_limits<float>::max)();

		for (const auto& glyph : renderer.runtimeLayout.glyphs) {
			minX = (std::min)(minX, glyph.rectMin.x);
			maxX = (std::max)(maxX, glyph.rectMax.x);
			minY = (std::min)(minY, glyph.rectMin.y);
			maxY = (std::max)(maxY, glyph.rectMax.y);
		}

		// グリフ矩形はピボット未適用なので、描画側と同じオフセットを加えて判定位置を合わせる
		// 正規化0-1基準のpivotがブロック全体のboundsSize上のこの点を原点へ寄せる
		const float pivotOffsetX = -renderer.pivot.x * renderer.runtimeLayout.boundsSize.x;
		const float pivotOffsetY = -renderer.pivot.y * renderer.runtimeLayout.boundsSize.y;
		minX += pivotOffsetX;
		maxX += pivotOffsetX;
		minY += pivotOffsetY;
		maxY += pivotOffsetY;

		if (hitPoint.x >= minX && hitPoint.x <= maxX &&
			hitPoint.y >= minY && hitPoint.y <= maxY) {
			hits.push_back({ entity, renderer.layer, renderer.order });
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
	[[maybe_unused]] const EditorContext& context, const RenderPipelineRunner& renderPipeline) {

	// 以下の条件のいずれかを満たす場合はピック処理を行わない
	if (!initialized_ || layoutState_.hidePanels || !editorState_.enableScenePick) {
		return;
	}

	Input* input = Input::GetInstance();

	// クリック(ドラッグせず離した)時に選択を確定する、候補が未解決ならreadback到着時に確定する
	if (input->ReleaseMouse(MouseButton::Left)) {

		const bool overViewport = editorState_.sceneViewportHovered || editorState_.gameViewportHovered;
		if (overViewport && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)) {

			editorState_.scenePickClickPending = true;
			editorState_.scenePickClickAdditive = ImGui::IsKeyDown(ImGuiKey_LeftShift);
			// 即時候補や空クリックはreadback待ちが無ければその場で確定する
			if (!meshSubMeshPicker_->HasPendingReadback() && context.activeWorld) {
				editorState_.CommitScenePick(*context.activeWorld);
			}
		}
	}

	auto executePick = [&](InputViewArea inputArea, RenderViewKind viewKind,
		ID3D12Resource* tlasResource, const std::vector<MeshSubMeshPickRecord>& pickRecords) {

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
			// 押すたびに候補と保留クリックをリセットする、選択はリリースまで遅延する
			editorState_.scenePickDragEntity = Entity::Null();
			editorState_.scenePickClickPending = false;

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
				};

			// SceneView専用Overlayは通常2D/TLASより優先してEntity単位で選択する
			if (viewKind == RenderViewKind::Scene) {
				Entity overlayHit = Entity::Null();
				if (sceneComponentOverlayPicker_.Pick(context.activeWorld,
					renderPipeline.GetResolvedView(viewKind), mousePosInView.value(), overlayHit)) {
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

			// メッシュピック処理を実行、シフト状態は結果消費時のトグル判定に使う
			meshSubMeshPicker_->ExecutePick(graphicsCore, renderPipeline.GetResolvedView(viewKind),
				mousePosInView.value(), pickRecords, tlasResource, false, false);
			return true;
		};

	// SceneViewは従来通り、ギズモ操作中はピックしない
	if (layoutState_.showSceneView && !editorState_.useSceneGizmo) {
		if (executePick(InputViewArea::Scene, RenderViewKind::Scene,
			renderPipeline.GetSceneViewTLASResource(), renderPipeline.GetSceneViewPickRecords())) {
			return;
		}
	}

	// GameViewにもSceneViewと同じTLASピックだけを通し、マニピュレーターは表示しない
	if (layoutState_.showGameView) {
		executePick(InputViewArea::Game, RenderViewKind::Game,
			renderPipeline.GetGameViewTLASResource(), renderPipeline.GetGameViewPickRecords());
	}
}


