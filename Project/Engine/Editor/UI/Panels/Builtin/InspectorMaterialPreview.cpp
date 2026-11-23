#include "InspectorMaterialPreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <cmath>

namespace {

	constexpr int kWidth = 480;
	constexpr int kHeight = 300;
	constexpr Engine::AssetID kPreviewMeshes[] = {
		{0xd5da6a84963e4926, 0xa217b683acb6ec0e},
		{0xd5089662500843d1, 0x9d9e21d761d57e6f},
		{0x522eff67a5284d07, 0x9128206abb3198c7},
	};

	bool IsFlat(const Engine::MaterialAsset& material) {
		return material.domain == Engine::MaterialDomain::UI || material.usage == Engine::MaterialUsage::Sprite ||
			material.usage == Engine::MaterialUsage::Text;
	}

	Engine::AssetID FindFont(const Engine::AssetDatabase& database) {

		const Engine::AssetMeta* first = nullptr;
		for (const auto& [id, meta] : database.GetAssets()) {
			if (meta.type == Engine::AssetType::Font && (!first || meta.assetPath < first->assetPath)) first = &meta;
		}
		return first ? first->guid : Engine::AssetID{};
	}
}

void Engine::InspectorMaterialPreview::Rebuild(const EditorPanelContext& context, const MaterialAsset& material) {

	world_ = std::make_unique<ECSWorld>();
	missingFont_ = false;
	root_ = world_->CreateEntity(UUID::New());
	world_->AddComponent<TransformComponent>(root_);
	HierarchySystem hierarchy;
	const auto create = [&](Vector3 position, Vector3 scale = Vector3::AnyInit(1.0f)) {
		const Entity entity = world_->CreateEntity(UUID::New());
		hierarchy.SetParent(*world_, entity, root_);
		auto& transform = world_->AddComponent<TransformComponent>(entity);
		transform.localPos = position;
		transform.localScale = scale;
		transform.worldMatrix = MakeLocalMatrix(transform);
		transform.isDirty = false;
		return entity;
	};

	const bool flat = IsFlat(material);
	const Entity subject = create({});
	if (material.domain == MaterialDomain::Decal) {
		auto& receiver = world_->AddComponent<PrimitiveRendererComponent>(subject);
		receiver.type = PrimitiveType::Cube;
		receiver.cube.size = {2.6f, 2.6f, 0.16f};
		const Entity projector = create({});
		auto& decal = world_->AddComponent<DecalProjectorComponent>(projector);
		decal.material = material.guid;
		decal.size = {2.0f, 2.0f, 0.4f};
	} else if (material.usage == MaterialUsage::Text) {
		auto& text = world_->AddComponent<TextRendererComponent>(subject);
		text.font = FindFont(*context.editorContext->assetDatabase);
		missingFont_ = !text.font;
		text.material = material.guid;
		text.text = "Aa";
		text.fontSize = 120.0f;
		text.pivot = Vector2::AnyInit(0.5f);
	} else if (material.usage == MaterialUsage::Sprite) {
		auto& sprite = world_->AddComponent<SpriteRendererComponent>(subject);
		sprite.material = material.guid;
		sprite.size = Vector2::AnyInit(208.0f);
	} else if (material.usage == MaterialUsage::Mesh) {
		auto& mesh = world_->AddComponent<MeshRendererComponent>(subject);
		mesh.mesh = kPreviewMeshes[shape_];
		mesh.material = material.guid;
	} else {
		auto& primitive = world_->AddComponent<PrimitiveRendererComponent>(subject);
		primitive.material = material.guid;
		primitive.type = flat || shape_ == 2 ? PrimitiveType::Plane : shape_ == 0 ? PrimitiveType::Sphere : PrimitiveType::Cube;
		primitive.renderSpace = flat ? PrimitiveRenderSpace::Screen2D : PrimitiveRenderSpace::World3D;
		primitive.plane.size = Vector2::AnyInit(flat ? 208.0f : 2.0f);
		primitive.cube.size = Vector3::AnyInit(1.44f);
		if (flat) primitive.queue = RenderPhase::ScreenUI;
	}

	// 背景の縦縞と床の境目で透過と屈折を比較する
	const auto backdrop = [&](Vector3 position, Vector3 size, Color4 color) {
		const Entity entity = create(position);
		auto& block = world_->AddComponent<PrimitiveRendererComponent>(entity);
		block.type = PrimitiveType::Cube;
		block.cube.size = size;
		block.materialInstance.Set(MaterialParameterIDs::BaseColor, MaterialParameterNames::BaseColor,
			MaterialParameterSemantic::BaseColor, MaterialParameterValue{.value = color});
		block.receiveDecals = false;
	};
	for (int i = -5; i <= 5; ++i) {
		const Color4 color = i % 2 ? Color4(0.11f, 0.16f, 0.21f, 1.0f) : Color4(0.42f, 0.36f, 0.26f, 1.0f);
		backdrop({0.65f * i, 1.0f, 2.3f}, {0.65f, 7.0f, 0.12f}, color);
	}
	backdrop({0.0f, -1.18f, 0.0f}, {15.0f, 0.14f, 15.0f}, Color4(0.16f, 0.17f, 0.18f, 1.0f));
	const Entity light = create({});
	auto& sun = world_->AddComponent<DirectionalLightComponent>(light);
	sun.direction = Vector3(0.4f, -0.6f, 0.7f).Normalize();
	sun.intensity = 2.4f;
	sun.shadowStrength = 0.0f;
	rendered_ = false;
}

void Engine::InspectorMaterialPreview::ResetView() {

	yaw_ = 0.0f;
	pitch_ = 8.0f;
	distance_ = 4.3f;
	rendered_ = false;
}

void Engine::InspectorMaterialPreview::Draw(const EditorPanelContext& context, const MaterialAsset& material) {

	if (!MyGUI::CollapsingHeader("プレビュー", true)) return;
	if (!context.graphicsCore || !context.renderPipeline || !context.editorContext || !context.editorContext->assetDatabase) return;
	if (material.domain == MaterialDomain::Fullscreen || material.domain == MaterialDomain::Compute ||
		material.domain == MaterialDomain::RayTracing || material.usage == MaterialUsage::Line || material.usage == MaterialUsage::Particle) {
		ImGui::TextDisabled("この描画対象はマテリアルプレビューに対応していません");
		return;
	}

	auto& database = *context.editorContext->assetDatabase;
	if (materialID_ != material.guid || domain_ != material.domain || usage_ != material.usage) {
		materialID_ = material.guid;
		domain_ = material.domain;
		usage_ = material.usage;
		databaseLifetime_ = database.GetCacheLifetime();
		shape_ = 0;
		time_ = 0.0f;
		playing_ = false;
		ResetView();
		Rebuild(context, material);
	} else if (databaseLifetime_.lock() != database.GetCacheLifetime().lock()) {
		databaseLifetime_ = database.GetCacheLifetime();
		Rebuild(context, material);
	}

	const bool flat = IsFlat(material);
	if (missingFont_) {
		ImGui::TextDisabled("文字のプレビューにはフォントアセットが必要です");
		return;
	}
	if (!flat && material.domain != MaterialDomain::Decal) {
		ImGui::SetNextItemWidth(70.0f);
		if (ImGui::Combo("##PreviewShape", &shape_, "球\0立方体\0平面\0")) Rebuild(context, material);
		ImGui::SameLine();
	}
	if (ImGui::Button(playing_ ? "一時停止" : "再生")) playing_ = !playing_;
	ImGui::SameLine();
	if (ImGui::Button("視点をリセット")) ResetView();

	const float width = (std::max)(ImGui::GetContentRegionAvail().x, 1.0f);
	const ImVec2 size(width, width * kHeight / kWidth);
	const ImVec2 pos = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton("##MaterialPreview", size);
	ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
	const ImGuiIO& io = ImGui::GetIO();
	if (!flat && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		yaw_ -= io.MouseDelta.x * 0.5f;
		pitch_ = std::clamp(pitch_ + io.MouseDelta.y * 0.5f, -85.0f, 85.0f);
		rendered_ = false;
	}
	if (ImGui::IsItemHovered() && io.MouseWheel != 0.0f) {
		distance_ = std::clamp(distance_ * std::pow(0.88f, io.MouseWheel), 1.6f, 10.0f);
		rendered_ = false;
	}

	EditorToolContext tool{};
	tool.panelContext = &context;
	resources_.BeginEditorToolFrame(tool);
	auto* output = resources_.CreateRenderTexture("InspectorMaterialPreview", {kWidth, kHeight}, Color4::Black(),
		1, true, DXGI_FORMAT_R16G16B16A16_FLOAT);
	if (output) {
		const auto revision = context.graphicsCore->GetTextureUploadService().GetContentRevision();
		if (!rendered_ || playing_ || revision != textureRevision_ || size.x != displaySize_.x) {
			SystemContext system{};
			system.world = world_.get();
			system.assetDatabase = &database;
			system.renderAssetLibrary = &context.renderPipeline->GetRenderAssetLibrary();
			system.deltaTime = system.smoothDeltaTime = system.unscaledDeltaTime = playing_ ? io.DeltaTime : 0.0f;
			time_ += system.deltaTime;
			system.time = system.unscaledTime = time_;
			EntityPreviewRenderRequest request{};
			request.world = world_.get();
			request.rootEntity = root_;
			request.surface = output->GetRenderTarget();
			request.systemContext = &system;
			request.assetDatabase = &database;
			request.materialPreview = true;
			auto& camera = request.camera;
			camera.enableOrthographic = flat;
			camera.orthographicZoom = 4.3f / distance_;
			camera.transform2D.pos = {-0.5f * kWidth / camera.orthographicZoom, -0.5f * kHeight / camera.orthographicZoom, 0.0f};
			camera.perspectiveFovY = 36.0f;
			camera.transform3D.rotation = {pitch_, yaw_, 0.0f};
			const auto rotation = Matrix4x4::MakeRotateMatrix(camera.transform3D.rotation);
			camera.transform3D.pos = Vector3(rotation.m[2][0], rotation.m[2][1], rotation.m[2][2]) * -distance_;
			rendered_ = context.renderPipeline->RenderEntityPreview(*context.graphicsCore, request);
			textureRevision_ = revision;
			displaySize_ = size;
		}
		if (rendered_) {
			ImGui::GetWindowDrawList()->AddImage(output->GetImTextureID(), pos, ImVec2(pos.x + size.x, pos.y + size.y));
		} else {
			ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), "プレビューを読み込み中…");
		}
	}
	resources_.EndEditorToolFrame();
}
