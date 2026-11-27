#include "MaterialPaintTool.h"
//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialPaintMask.h>
#include <Engine/Core/Rendering/Materials/MaterialPaintPreview.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <algorithm>
#include <cmath>

namespace {

	bool IsPaintable(const Engine::EditorPanelContext& context, const Engine::MeshRendererComponent* mesh,
		const Engine::PrimitiveRendererComponent* primitive, const Engine::SubMeshMaterial* subMesh) {

		using namespace Engine;
		MeshSubMeshRenderState state{};
		if (mesh && subMesh) state = MeshDrawPathCommon::ResolveSubMeshRenderState(*mesh, *subMesh);
		else {
			state.material = mesh ? mesh->material : primitive->material;
			state.surfaceMode = (mesh ? mesh->queue : primitive->queue) == RenderPhase::Opaque ?
				MaterialSurfaceMode::Opaque : MaterialSurfaceMode::Transparent;
		}
		return MaterialRenderPolicy::IsMaterialPaintSurface(*context.editorContext->assetDatabase,
			context.renderPipeline->GetRenderAssetLibrary(), state, mesh ? DefaultMaterialSlot::Mesh : DefaultMaterialSlot::Primitive);
	}
}

const char* Engine::MaterialPaintTool::HoverDiagnostic(const EditorPanelContext& context, Entity object, uint32_t subMesh) {

	auto& world = *context.GetWorld();
	if (!world.IsAlive(object)) return nullptr;
	if (!context.editorState->IsEntitySelected(object) && context.editorState->selectedEntity != object)
		return "塗るオブジェクトを選択してください";
	if (world.HasComponent<SkinnedAnimationComponent>(object)) return "スキニングメッシュはペイント対象外です";
	const auto* mesh = world.TryGetComponent<MeshRendererComponent>(object);
	const auto* primitive = world.TryGetComponent<PrimitiveRendererComponent>(object);
	if ((!mesh && !primitive) || (mesh && primitive) || (primitive && IsPrimitiveScreen2D(*primitive)))
		return "静的メッシュまたは3Dプリミティブを使用してください";
	const auto subMeshes = GetMeshSubMeshes(static_cast<const ECSWorld&>(world), object);
	if (!IsPaintable(context, mesh, primitive, subMesh < subMeshes.size() ? &subMeshes[subMesh] : nullptr))
		return "不透明の標準PBRマテリアルを使用してください";
	return nullptr;
}

void Engine::MaterialPaintTool::BeginStroke(const EditorPanelContext& context, bool erase) {

	if (waitingForMask_) { ClearPreviews(); waitingForMask_ = false; }
	if (!preparingStroke_) targets_.clear();
	preparingStroke_ = false;
	diagnostic_.clear();
	hasLastPoint_ = false;
	if (MaterialPaintSaveQueueFull()) {
		diagnostic_ = "ペイントマスクの保存を待っています";
		pendingPress_ = preparingStroke_ = true;
		return;
	}
	if (!activeMaterial_) {
		diagnostic_ = "塗る素材を設定してください";
		return;
	}
	const auto* material = context.renderPipeline->GetRenderAssetLibrary().LoadMaterial(activeMaterial_);
	diagnostic_ = material ? MaterialRenderPolicy::GetMaterialPaintUnsupportedReason(*material) : "マテリアルを読み込めません";
	if (!diagnostic_.empty()) return;
	auto& world = *context.GetWorld();
	auto& db = *context.editorContext->assetDatabase;
	auto selection = context.editorState->GetSelectedEntities();
	if (selection.empty() && world.IsAlive(context.editorState->selectedEntity))
		selection.push_back(context.editorState->selectedEntity);
	const auto* camera = context.sceneRenderView->FindCamera(RenderCameraDomain::Perspective);
	for (Entity object : selection) {
		if (!world.IsAlive(object) || !IsEntityActiveInHierarchy(world, object)) continue;
		const auto* scene = world.TryGetComponent<SceneObjectComponent>(object);
		if (scene && camera && !(scene->visibilityLayerMask & camera->cullingMask)) continue;
		const auto* transform = world.TryGetComponent<TransformComponent>(object);
		const auto* mesh = world.TryGetComponent<MeshRendererComponent>(object);
		const auto* primitive = world.TryGetComponent<PrimitiveRendererComponent>(object);
		if (!transform || (!mesh && !primitive) || (mesh && primitive) || world.HasComponent<SkinnedAnimationComponent>(object)) continue;
		if ((mesh && (!mesh->visible || !mesh->mesh)) || (primitive && (!primitive->visible || IsPrimitiveScreen2D(*primitive)))) continue;

		PaintTarget target;
		target.edit.object = world.GetUUID(object);
		if (std::ranges::any_of(targets_, [&](const PaintTarget& existing) { return existing.edit.object == target.edit.object; })) continue;
		auto& paint = target.edit.after;
		if (const auto* stored = world.TryGetComponent<MaterialPaintComponent>(object)) {
			target.edit.before = *stored;
			paint = *stored;
		} else {
			paint.projectionFromLocal = transform->worldMatrix;
			paint.resolution = resolution_;
		}
		const auto subMeshes = GetMeshSubMeshes(static_cast<const ECSWorld&>(world), object);
		for (const auto& sub : subMeshes) {
			if (!mesh) break;
			target.allowed.push_back(sub.visible && IsPaintable(context, mesh, primitive, &sub));
			target.matrices.emplace_back(MeshSubMeshRuntime::BuildRenderLocalMatrix(sub) * transform->worldMatrix);
		}
		if (target.allowed.empty()) {
			target.allowed.push_back(IsPaintable(context, mesh, primitive, nullptr));
			target.matrices.emplace_back(transform->worldMatrix);
		}
		if (std::none_of(target.allowed.begin(), target.allowed.end(), [](bool value) { return value; })) continue;
		bool preparing = false;
		target.atlas = ResolveMaterialPaintAtlas(db, mesh ? mesh->mesh : AssetID{}, primitive, paint.atlas, diagnostic_, &preparing);
		if (preparing) { pendingPress_ = preparingStroke_ = true; return; }
		if (!target.atlas) continue;
		target.worldBatches.reserve(target.atlas->batches.size());
		for (auto batch : target.atlas->batches) {
			const auto sub = target.matrices.size() == 1 ? 0 : target.atlas->triangles[batch.first].subMesh;
			if (target.matrices.size() <= sub || !target.allowed[sub]) continue;
			const auto& m = target.matrices[sub];
			const Vector3 center = Vector3::Transform((batch.min + batch.max) * 0.5f, m);
			const Vector3 extent = (batch.max - batch.min) * 0.5f;
			const auto axis = [&](size_t i) { return std::abs(m.m[0][i]) * extent.x +
				std::abs(m.m[1][i]) * extent.y + std::abs(m.m[2][i]) * extent.z; };
			const Vector3 worldExtent(axis(0), axis(1), axis(2));
			batch.min = center - worldExtent;
			batch.max = center + worldExtent;
			target.worldBatches.push_back(batch);
		}
		const auto hash = std::to_string(target.atlas->geometryHash);
		if (!paint.geometryKey.empty() && paint.geometryKey != hash) {
			diagnostic_ = "モデルの形状が変更されています";
			continue;
		}
		paint.geometryKey = hash;
		const auto it = std::find_if(paint.layers.begin(), paint.layers.end(),
			[&](const MaterialPaintLayer& layer) { return layer.material == activeMaterial_; });
		const bool newLayer = it == paint.layers.end();
		if (newLayer) {
			if (erase) continue;
			if (paint.layers.size() >= 4) {
				diagnostic_ = "素材レイヤーは4つまでです";
				continue;
			}
			while (std::any_of(paint.layers.begin(), paint.layers.end(),
				[&](const MaterialPaintLayer& layer) { return layer.channel == target.channel; })) ++target.channel;
			paint.layers.push_back({activeMaterial_, target.channel});
		} else {
			if (!it->visible) { diagnostic_ = "選択した素材レイヤーは非表示です"; continue; }
			target.channel = it->channel;
		}
		if (target.channel >= 4 || !LoadMaterialPaintMask(db, paint.mask, paint.resolution, target.mask, &preparing)) {
			if (preparing) { pendingPress_ = preparingStroke_ = true; return; }
			diagnostic_ = "ペイントマスクを読み込めません";
			continue;
		}
		if (newLayer && paint.mask) {
			for (size_t i = target.channel; i < target.mask.size(); i += 4) target.mask[i] = 0;
		} else if (paint.mask) {
			const auto previous = std::ranges::find(savedPreviews_, paint.mask, &SavedPreview::mask);
			if (previous != savedPreviews_.end()) {
				target.preview = std::move(previous->texture);
				savedPreviews_.erase(previous);
			}
		}
		targets_.emplace_back(std::move(target));
	}
	stroke_ = !targets_.empty();
	if (!stroke_ && diagnostic_.empty()) diagnostic_ = "不透明の標準PBRを使用するメッシュまたは3Dプリミティブを選択してください";
}

void Engine::MaterialPaintTool::FinishStroke(const EditorPanelContext& context) {

	std::vector<MaterialPaintEdit> edits;
	for (auto& target : targets_) {
		if (!target.changed) continue;
		auto& paint = target.edit.after;
		auto& database = *context.editorContext->assetDatabase;
		paint.mask = QueueMaterialPaintMask(database, paint.resolution, std::move(target.mask), target.atlas);
		maskDatabase_ = &database;
		maskDatabaseLife_ = database.GetCacheLifetime();
		if (!paint.mask) {
			diagnostic_ = "ペイントマスクを保存できません";
			edits.clear();
			break;
		}
		edits.push_back(target.edit);
	}
	const bool committed = !edits.empty() && context.host->ExecuteEditorCommand(std::make_unique<MaterialPaintCommand>(std::move(edits)));
	if (!committed) {
		if (diagnostic_.empty() && std::ranges::any_of(targets_, &PaintTarget::changed)) diagnostic_ = "ペイントを適用できません";
		ClearPreviews();
		targets_.clear();
	} else {
		waitingForMask_ = true;
		savedPreviews_.clear();
		for (const auto& target : targets_) {
			if (target.changed && target.preview) savedPreviews_.push_back({target.edit.after.mask, target.preview});
		}
	}
	stroke_ = hasLastPoint_ = false;
	strokeView_ = 0;
}

namespace Engine {

	void MaterialPaintTool::ResolveCommittedMasks(const EditorPanelContext& context) {

		auto& world = *context.GetWorld();
		bool pending = false;
		auto& database = *context.editorContext->assetDatabase;
		for (const auto& target : targets_) {
				if (!target.changed || !target.preview) continue;
				const Entity object = world.FindByUUID(target.edit.object);
				const auto* current = world.IsAlive(object) ? world.TryGetComponent<MaterialPaintComponent>(object) : nullptr;
				if (!current || current->mask != target.edit.after.mask) { pending = false; break; }
				if (!database.Find(current->mask)) { pending = true; continue; }
				const auto loaded = RuntimeTextureResolver::ResolveBindless(*context.graphicsCore,
					context.editorContext->assetDatabase, current->mask, TextureColorSpace::Linear);
				pending |= loaded.retry;
		}
		if (!pending) {
			ClearPreviews();
			targets_.clear();
			waitingForMask_ = false;
		}
	}

	void MaterialPaintTool::ClearPreviews() {

		const auto life = world_.lock();
		if (previewWorld_ && life && life->IsAlive()) ClearMaterialPaintPreviews(*previewWorld_);
		for (auto& target : targets_) {
			if (target.previewID) RuntimeTextureResolver::UnregisterRenderTexture(target.previewID, target.preview.get());
		}
		previewWorld_ = nullptr;
	}

	void MaterialPaintTool::UpdatePreviews(const EditorPanelContext& context) {

		auto& graphics = *context.graphicsCore;
		auto& dx = graphics.GetDXObject();
		auto& world = *context.GetWorld();
		for (auto& target : targets_) {
			if (target.dirty.Empty()) continue;
			const uint32_t size = target.edit.after.resolution;
			if (!target.preview) {
				auto texture = std::make_shared<RenderTexture2D>();
				texture->Create(dx.GetDevice(), &graphics.GetRTVDescriptor(), &graphics.GetSRVDescriptor(),
					{.width = size, .height = size, .format = DXGI_FORMAT_R8G8B8A8_UNORM, .clearColor = Color4(0,0,0,0), .debugName = L"MaterialPaintPreview"});
				if (target.edit.before && target.edit.before->mask) target.dirty = {0, 0, size, size};
				else {
					constexpr float empty[4]{};
					auto* cmd = dx.GetDxCommand();
					texture->Transition(*cmd, D3D12_RESOURCE_STATE_RENDER_TARGET);
					cmd->GetCommandList()->ClearRenderTargetView(texture->GetRenderTarget().rtvHandle, empty, 0, nullptr);
				}
				target.preview = std::move(texture);
			}
			if (!target.previewID) {
				target.previewID = AssetID::New();
				RuntimeTextureResolver::RegisterRenderTexture(target.previewID, target.preview.get(),
					TextureOwnership(target.preview, reinterpret_cast<const uint8_t*>(target.preview.get())));
			}
			const auto region = target.dirty;
			const uint32_t width = region.right - region.left, height = region.bottom - region.top;
			const uint32_t pitch = (4 * width + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
			const auto upload = previewUploads_.Reserve(dx.GetResourceRetirement(), dx.GetDevice(),
				size_t(height) * pitch, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
			for (uint32_t y = 0; y < height; ++y) {
				const size_t offset = (size_t(region.top + y) * size + region.left) * 4;
				std::memcpy(upload.mapped + size_t(y) * pitch, target.mask.data() + offset, size_t(width)*4);
			}
			auto* cmd = dx.GetDxCommand();
			target.preview->Transition(*cmd, D3D12_RESOURCE_STATE_COPY_DEST);
			D3D12_TEXTURE_COPY_LOCATION src{};
			src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.pResource = upload.resource;
			src.PlacedFootprint.Offset = upload.offset;
			src.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, pitch};
			D3D12_TEXTURE_COPY_LOCATION dst{};
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.pResource = target.preview->GetResource();
			cmd->GetCommandList()->CopyTextureRegion(&dst, region.left, region.top, 0, &src, nullptr);
			target.preview->Transition(*cmd, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
			target.dirty = {};
			auto component = target.edit.after;
			component.mask = target.previewID;
			SetMaterialPaintPreview(world, world.FindByUUID(target.edit.object), component);
			previewWorld_ = &world;
		}
	}

}
