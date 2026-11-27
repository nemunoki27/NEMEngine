#include "InspectorMaterialPreview.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/EditorIconButton.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>
#include <Engine/Core/Rendering/Particle/Structures/ParticleMaterialCompatibility.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
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

	Engine::MaterialUsage ResolvePreviewUsage(const Engine::MaterialAsset& material) {

		using namespace Engine;
		if (material.usage != MaterialUsage::Generic) return material.usage;
		// usage未記録の標準素材は、実際のパイプラインに対応するRendererを使う
		for (const auto& pass : material.passes) {
			using namespace BuiltinAssets::Pipelines;
			if (pass.pipeline == DefaultSprite) return MaterialUsage::Sprite;
			if (pass.pipeline == DefaultText) return MaterialUsage::Text;
			if (pass.pipeline == DefaultLine) return MaterialUsage::Line;
			if (pass.pipeline == DefaultMesh || pass.pipeline == DefaultMeshMasked || pass.pipeline == DefaultMeshTransparent)
				return MaterialUsage::Mesh;
		}
		return MaterialUsage::Generic;
	}

	bool IsFlat(const Engine::MaterialAsset& material) {

		using namespace Engine;
		const auto usage = ResolvePreviewUsage(material);
		const auto* pass = FindPass(material, MaterialPassKind::Draw);
		return material.domain == MaterialDomain::UI || usage == MaterialUsage::Sprite || usage == MaterialUsage::Text ||
			(pass && pass->pipeline == BuiltinAssets::Pipelines::DefaultPrimitive2D) || material.guid == BuiltinAssets::Materials::ProgressPrimitive;
	}

	constexpr Engine::AssetID kPreviewFont{0x9422a58504c749af, 0x8109eaa8cb648c42};

	bool IsTrailMaterial(const Engine::MaterialAsset& material) {

		const auto* pass = FindPass(material, Engine::MaterialPassKind::Transparent);
		return material.usage == Engine::MaterialUsage::Particle && pass &&
			Engine::IsTrailMaterialPass(material, *pass);
	}

	void ApplyStandardAppearance(Engine::PrimitiveRendererComponent& renderer, const Engine::MaterialAsset& material) {

		renderer.material = Engine::BuiltinAssets::Materials::DefaultPrimitive;
		const auto sample = MakePaintMaterialSample(material);
		renderer.materialInstance.Set(Engine::MaterialParameterIDs::BaseColor, Engine::MaterialParameterNames::BaseColor,
			Engine::MaterialParameterSemantic::BaseColor, {.value = sample.baseColor});
		renderer.materialInstance.Set(Engine::MaterialParameterIDs::BaseColorTexture, Engine::MaterialParameterNames::BaseColorTexture,
			Engine::MaterialParameterSemantic::BaseColorTexture, {.value = sample.baseColorTexture});
	}

	void SamplePreviewTrail(Engine::TrailRendererRecord& trail, float deltaTime) {

		AgeTrail(trail, deltaTime);
		const float angle = static_cast<float>(trail.runtime.time) * 2.8f;
		const Engine::Vector3 point{1.05f * std::cos(angle), 0.58f * std::sin(angle), 0.3f * std::sin(2.0f * angle)};
		AppendTrailSample(trail, point, point, {0.0f, 0.0f, 1.0f});
	}
}

void Engine::InspectorMaterialPreview::Rebuild(const EditorPanelContext& context, const MaterialAsset& material, bool thumbnail) {

	world_ = std::make_unique<ECSWorld>();
	missingFont_ = false;
	effectSubject_ = Entity::Null();
	trailSubject_ = IsTrailMaterial(material);
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
	const auto usage = ResolvePreviewUsage(material);
	const Entity subject = create({});
	subject_ = subject;
	if (material.domain == MaterialDomain::Decal) {
		auto& receiver = world_->AddComponent<PrimitiveRendererComponent>(subject);
		receiver.type = PrimitiveType::Cube;
		receiver.cube.size = {2.6f, 2.6f, 0.16f};
		const Entity projector = create({});
		auto& decal = world_->AddComponent<DecalProjectorComponent>(projector);
		decal.material = material.guid;
		decal.size = {2.0f, 2.0f, 0.4f};
	} else if (material.guid == BuiltinAssets::Materials::DefaultBeam) {
		effectSubject_ = subject;
		world_->AddComponent<BeamRendererComponent>(subject);
		BeamRendererSettings settings{};
		settings.material = material.guid;
		settings.start.position = {-1.1f, -0.45f, 0.0f};
		settings.end.position = {1.1f, 0.45f, 0.0f};
		settings.width = 0.28f;
		SetBeamRendererSettings(*world_, subject, settings);
		PlayBeam(*world_, subject);
	} else if (trailSubject_) {
		effectSubject_ = subject;
		world_->AddComponent<TrailRendererComponent>(subject);
		TrailRendererSettings settings{};
		settings.material = material.guid;
		settings.lifetime = 0.85f;
		settings.width = 0.36f;
		settings.minVertexDistance = 0.018f;
		SetTrailRendererSettings(*world_, subject, settings);
		if (auto* trail = TryGetTrailRenderer(*world_, subject)) {
			for (int i = 0; i < 48; ++i) SamplePreviewTrail(*trail, 1.0f / 60.0f);
		}
	} else if (material.usage == MaterialUsage::Particle) {
		effectSubject_ = subject;
		auto& component = world_->AddComponent<ParticleSystemComponent>(subject);
		component.useAutoRandomSeed = false;
		component.randomSeed = 173u;
		auto settings = MakeDefaultParticleSystemSettings();
		settings.material = material.guid;
		settings.blendMode = material.renderState.blendMode;
		settings.emitter.shape = ParticleEmitterShape::Sphere;
		settings.emitter.sphere.radius = 0.35f;
		settings.emitter.speed = ParticleValue<float>{0.65f};
		settings.emitter.emitInterval = 0.09f;
		settings.emitter.emitCount = ParticleValue<uint32_t>{1u};
		settings.emitter.maxParticles = 24;
		settings.plane.size = Vector2::AnyInit(0.5f);
		settings.phases.front().modules.clear();
		settings.phases.front().lifetime = ParticleValue<float>{1.2f};
		SetParticleSystemSettings(*world_, subject, settings);
		SystemContext warmup{};
		warmup.world = world_.get();
		warmup.mode = WorldMode::Play;
		warmup.deltaTime = warmup.unscaledDeltaTime = 1.0f / 60.0f;
		for (int i = 0; i < 60; ++i) particles_.Update(*world_, warmup);
	} else if (usage == MaterialUsage::Line) {
		auto& sphere = world_->AddComponent<PrimitiveRendererComponent>(subject);
		sphere.type = PrimitiveType::Sphere;
		ApplyStandardAppearance(sphere, material);
	} else if (usage == MaterialUsage::Text) {
		auto& text = world_->AddComponent<TextRendererComponent>(subject);
		text.font = kPreviewFont;
		missingFont_ = !context.editorContext->assetDatabase->Find(kPreviewFont);
		text.material = material.guid;
		text.text = "Aa";
		text.fontSize = 120.0f;
		text.pivot = Vector2::AnyInit(0.5f);
	} else if (usage == MaterialUsage::Sprite) {
		auto& sprite = world_->AddComponent<SpriteRendererComponent>(subject);
		sprite.material = material.guid;
		sprite.size = Vector2::AnyInit(208.0f);
	} else if (usage == MaterialUsage::Mesh) {
		auto& mesh = world_->AddComponent<MeshRendererComponent>(subject);
		mesh.mesh = kPreviewMeshes[shape_];
		mesh.material = material.guid;
	} else {
		auto& primitive = world_->AddComponent<PrimitiveRendererComponent>(subject);
		if (!paintSample_ && (material.domain == MaterialDomain::Surface || flat)) primitive.material = material.guid;
		primitive.type = flat || shape_ == 2 ? PrimitiveType::Plane : shape_ == 0 ? PrimitiveType::Sphere : PrimitiveType::Cube;
		primitive.renderSpace = flat ? PrimitiveRenderSpace::Screen2D : PrimitiveRenderSpace::World3D;
		primitive.plane.size = Vector2::AnyInit(flat ? 208.0f : 2.0f);
		primitive.cube.size = Vector3::AnyInit(1.44f);
		if (flat) {
			primitive.queue = RenderPhase::ScreenUI;
			// XY平面の表とUVの上端を正射影カメラに合わせる
			auto& transform = world_->GetComponent<TransformComponent>(subject);
			transform.localRotation = Quaternion::FromEulerDegrees({180.0f, 0.0f, 0.0f});
			transform.worldMatrix = MakeLocalMatrix(transform);
		}
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
	if (!thumbnail || material.domain == MaterialDomain::Fullscreen || material.domain == MaterialDomain::Compute ||
		material.domain == MaterialDomain::RayTracing) {
		for (int i = -5; i <= 5; ++i) {
			const Color4 color = i % 2 ? Color4(0.11f, 0.16f, 0.21f, 1.0f) : Color4(0.42f, 0.36f, 0.26f, 1.0f);
			backdrop({0.65f * i, 1.0f, 2.3f}, {0.65f, 7.0f, 0.12f}, color);
		}
		backdrop({0.0f, -1.18f, 0.0f}, {15.0f, 0.14f, 15.0f}, Color4(0.16f, 0.17f, 0.18f, 1.0f));
	}
	const Entity light = create({});
	auto& sun = world_->AddComponent<DirectionalLightComponent>(light);
	sun.direction = Vector3(0.4f, -0.6f, 0.7f).Normalize();
	sun.intensity = 2.4f;
	sun.shadowStrength = 0.0f;
	rendered_ = false;
}

void Engine::InspectorMaterialPreview::UpdateEffectSubject(SystemContext& system, const MaterialAsset& material) {

	if (material.usage != MaterialUsage::Particle || system.deltaTime <= 0.0f) return;
	if (trailSubject_) {
		if (auto* trail = TryGetTrailRenderer(*world_, effectSubject_)) {
			SamplePreviewTrail(*trail, system.deltaTime);
		}
	} else {
		system.mode = WorldMode::Play;
		particles_.Update(*world_, system);
	}
}

bool Engine::InspectorMaterialPreview::RenderThumbnail(const EditorPanelContext& context, const MaterialAsset& material,
	EditorToolRenderTexture& output, const Color4& background) {

	if (material.domain == MaterialDomain::RayTracing &&
		(!context.graphicsCore->GetDXObject().ShouldUseDispatchRays() ||
		context.renderPipeline->PrepareRaytracingPreview(*context.graphicsCore, material) != RaytracingPipelineReadiness::Ready)) return false;
	time_ = 0.5f;
	const bool customSurface = material.domain == MaterialDomain::Surface && ResolvePreviewUsage(material) == MaterialUsage::Generic &&
		!material.shaderGraph && !MaterialRenderPolicy::IsStandardSurface(material);
	if (IsFlat(material) || customSurface) {
		// 専用の頂点・インスタンス入力を球へ流用せず、標準色とテクスチャを表示する
		const MaterialAsset sphere{};
		Rebuild(context, sphere, true);
		auto& renderer = world_->GetComponent<PrimitiveRendererComponent>(subject_);
		ApplyStandardAppearance(renderer, material);
		return RenderSample(context, sphere, output, EntityPreviewKind::MaterialThumbnail, 0.0f, background, nullptr);
	}
	Rebuild(context, material, true);
	return !missingFont_ && RenderSample(context, material, output, EntityPreviewKind::MaterialThumbnail, 0.0f, background, nullptr);
}

bool Engine::InspectorMaterialPreview::RenderSample(const EditorPanelContext& context, const MaterialAsset& material,
	EditorToolRenderTexture& output, EntityPreviewKind kind, float deltaTime, const Color4& background, const PaintMaterialAsset* paint) {

	auto& database = *context.editorContext->assetDatabase;
	const bool flat = IsFlat(material);
	const bool effect = material.domain == MaterialDomain::Fullscreen || material.domain == MaterialDomain::Compute ||
		material.domain == MaterialDomain::RayTracing;
	SystemContext system{};
	system.world = world_.get();
	system.assetDatabase = &database;
	system.renderAssetLibrary = &context.renderPipeline->GetRenderAssetLibrary();
	system.deltaTime = system.smoothDeltaTime = system.unscaledDeltaTime = deltaTime;
	time_ += system.deltaTime;
	system.time = system.unscaledTime = time_;
	UpdateEffectSubject(system, material);
	EntityPreviewRenderRequest request{};
	request.world = world_.get();
	request.rootEntity = root_;
	request.surface = output.GetRenderTarget();
	request.systemContext = &system;
	request.assetDatabase = &database;
	request.kind = kind;
	request.useThumbnailTextures = kind == EntityPreviewKind::MaterialThumbnail;
	request.clearColor = background;
	if (effect) request.materialPreviewEffect = material.guid;
	if (paint) request.paintSample = {subject_, paint};
	auto& camera = request.camera;
	camera.enableOrthographic = flat;
	camera.orthographicZoom = 4.3f / distance_ * static_cast<float>(output.size.y) / kHeight;
	camera.transform2D.pos = {-0.5f * output.size.x / camera.orthographicZoom, -0.5f * output.size.y / camera.orthographicZoom, 0.0f};
	camera.perspectiveFovY = 36.0f;
	camera.transform3D.rotation = {pitch_, yaw_, 0.0f};
	const auto rotation = Matrix4x4::MakeRotateMatrix(camera.transform3D.rotation);
	camera.transform3D.pos = Vector3(rotation.m[2][0], rotation.m[2][1], rotation.m[2][2]) * -distance_;
	return context.renderPipeline->RenderEntityPreview(*context.graphicsCore, request);
}

void Engine::InspectorMaterialPreview::ResetView() {

	yaw_ = 0.0f;
	pitch_ = 8.0f;
	distance_ = 4.3f;
	rendered_ = false;
}

void Engine::InspectorMaterialPreview::Draw(const EditorPanelContext& context, const MaterialAsset& material) {

	if (MyGUI::CollapsingHeader("プレビュー", true)) DrawImage(context, material, {}, false);
}

void Engine::InspectorMaterialPreview::Draw(const EditorPanelContext& context, AssetID id, const PaintMaterialAsset& paint) {

	if (!MyGUI::CollapsingHeader("プレビュー", true)) return;
	MaterialAsset surface;
	surface.guid = id;
	DrawImage(context, surface, {}, false, &paint);
}

void Engine::InspectorMaterialPreview::DrawImage(const EditorPanelContext& context, const MaterialAsset& material,
	ImVec2 size, bool animate, const PaintMaterialAsset* paint) {

	const bool embedded = size.x > 0.0f;
	if (!context.graphicsCore || !context.renderPipeline || !context.editorContext || !context.editorContext->assetDatabase) return;
	const bool effect = material.domain == MaterialDomain::Fullscreen || material.domain == MaterialDomain::Compute ||
		material.domain == MaterialDomain::RayTracing;
	if (material.domain == MaterialDomain::RayTracing && !context.graphicsCore->GetDXObject().ShouldUseDispatchRays()) {
		const auto& support = context.graphicsCore->GetDXObject().GetFeatureController().GetSupport();
		ImGui::TextWrapped("%s", support.SupportsDispatchRaysPath()
			? "グラフィック設定の「マテリアル反射パス」を有効にしてください"
			: "このGPUはDXRに対応していません");
		return;
	}
	if (!embedded && (effect || material.usage == MaterialUsage::Particle)) {
		ImGui::TextDisabled("この描画対象はマテリアルプレビューに対応していません");
		return;
	}

	auto& database = *context.editorContext->assetDatabase;
	const bool paintModeChanged = paintSample_.has_value() != (paint != nullptr);
	if (paint && (!paintSample_ || *paintSample_ != *paint)) {
		paintSample_ = *paint;
		Invalidate();
	} else if (!paint) {
		paintSample_.reset();
	}
	if (!world_ || paintModeChanged || materialID_ != material.guid || domain_ != material.domain || usage_ != material.usage ||
		trailSubject_ != IsTrailMaterial(material)) {
		if (materialID_ != material.guid) hasImage_ = false;
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
		ImGui::TextDisabled("プレビュー用フォントを読み込めません");
		return;
	}
	if (!embedded) {
		if (!flat && material.domain != MaterialDomain::Decal) {
			ImGui::SetNextItemWidth(70.0f);
			if (ImGui::Combo("##PreviewShape", &shape_, "球\0立方体\0平面\0")) Rebuild(context, material);
			ImGui::SameLine();
		}
		const char* action = playing_ ? "一時停止" : "再生";
		const ImVec2 buttonSize(ImGui::CalcTextSize(action).x + 2.0f * ImGui::GetStyle().FramePadding.x, ImGui::GetFrameHeight());
		if (EditorIconButton::Draw("MaterialPlay", playing_ ? EditorIconButton::Playback::Pause : EditorIconButton::Playback::Play,
			action, playing_, buttonSize)) playing_ = !playing_;
		ImGui::SameLine();
		if (ImGui::Button("視点をリセット")) ResetView();
		const float width = (std::max)(ImGui::GetContentRegionAvail().x, 1.0f);
		size = {width, width * kHeight / kWidth};
	}
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
	const Vector2I desired = embedded ? Vector2I{(std::max)(1, static_cast<int>(std::round(size.x))),
		(std::max)(1, static_cast<int>(std::round(size.y)))} : Vector2I{kWidth, kHeight};
	if (pendingSize_ != desired) {
		pendingSize_ = desired;
		resizeDeadline_ = ImGui::GetTime() + 0.25;
	}
	const char* name = embedded ? "ShaderGraphOutput" : "InspectorMaterialPreview";
	auto* output = resources_.FindRenderTexture(name);
	if (embedded && material.domain == MaterialDomain::RayTracing && (!rendered_ || animate)) {
		const auto readiness = context.renderPipeline->PrepareRaytracingPreview(*context.graphicsCore, material);
		if (readiness != RaytracingPipelineReadiness::Ready) {
			auto* draw = ImGui::GetWindowDrawList();
			if (output && hasImage_) draw->AddImage(output->GetImTextureID(), pos, ImVec2(pos.x + size.x, pos.y + size.y));
			draw->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), readiness == RaytracingPipelineReadiness::Building ? "更新中…" : "更新失敗");
			resources_.EndEditorToolFrame();
			return;
		}
	}
	if (output && output->size != desired && ImGui::GetTime() >= resizeDeadline_) {
		resources_.DestroyRenderTexture(name);
		output = nullptr;
	}
	if (!output) {
		output = resources_.CreateRenderTexture(name, desired, Color4::Black(), 1, true, DXGI_FORMAT_R16G16B16A16_FLOAT, embedded);
		rendered_ = false;
		hasImage_ = false;
	}
	if (output) {
		const auto revision = context.graphicsCore->GetTextureUploadService().GetContentRevision();
		const bool advancing = embedded ? animate : playing_;
		if (!rendered_ || advancing || revision != textureRevision_) {
			rendered_ = RenderSample(context, material, *output, embedded ? EntityPreviewKind::ShaderGraph : EntityPreviewKind::Material,
				advancing ? io.DeltaTime : 0.0f, Color4::Black(), paint);
			hasImage_ = rendered_;
			textureRevision_ = revision;
		}
		if (rendered_) {
			ImGui::GetWindowDrawList()->AddImage(output->GetImTextureID(), pos, ImVec2(pos.x + size.x, pos.y + size.y));
		} else {
			ImGui::GetWindowDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), "プレビューを読み込み中…");
		}
	}
	resources_.EndEditorToolFrame();
}
