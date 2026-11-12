#include "InspectorPresentationTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Lighting/AtmosphereLighting.h>
#include <Engine/Core/World/Components/Lighting/SkyAtmosphereComponent.h>
#include <Engine/Core/World/Components/Lighting/ExponentialHeightFogComponent.h>
#include <Engine/Editor/UI/Inspectors/Common/InspectorCommonBindings.h>
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentEditSession.h>
#include <Engine/Editor/Commands/Components/RemoveComponentCommand.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Editor/UI/Inspectors/Core/ComponentEditorRegistry.h>
#include <Engine/Editor/UI/Common/SceneEntityReferenceFields.h>
#include <Engine/Editor/Settings/ProjectRenderingLayerSettings.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Physics/ParticleFieldComponents.h>
#include <Engine/Core/World/Components/Physics/PhysicsJointComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpersInternal.h>
#include <Engine/Editor/UI/Inspectors/Builtin/Script/ScriptFieldInspector.h>
#include <imgui_internal.h>

// c++
#include <array>
#include <cmath>
#include <limits>
#include <iostream>
#include <unordered_set>

namespace {

	// 既存データと複製でも表示フラグと共通末尾項目を維持する
	template<typename T>
	bool CheckDebugDrawSettings() {

		T component;
		auto fields = Engine::BindInspectorCommonFields(component);
		if (!fields.drawDebug || !*fields.drawDebug) return false;
		*fields.drawDebug = false;
		nlohmann::json saved = component;
		T restored = saved.get<T>();
		if (*Engine::BindInspectorCommonFields(restored).drawDebug) return false;
		saved.erase("drawDebug");
		from_json(saved, restored);
		return *Engine::BindInspectorCommonFields(restored).drawDebug;
	}
	class EmptyDrawer final : public Engine::IInspectorComponentDrawer {
	public:
		void Draw(const Engine::EditorPanelContext&, Engine::ECSWorld&, const Engine::Entity&) override {}
		bool CanDraw(Engine::ECSWorld&, const Engine::Entity&) const override { return false; }
	};

	class SimpleAnimationEditHooks final : public Engine::SerializedComponentEditHooks<Engine::SimpleAnimationComponent> {
	public:
		void OnSyncDraftFromWorld(Engine::ECSWorld&, const Engine::Entity&, const Engine::SimpleAnimationComponent&) override {}
		void OnBeforeCommit(const Engine::SimpleAnimationComponent&, Engine::SimpleAnimationComponent&) override {}
		void SerializeDraft(Engine::ECSWorld&, const Engine::Entity&, const Engine::SimpleAnimationComponent& component,
			nlohmann::json& out) const override { out = component; }
		void ApplyPreview(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::SimpleAnimationComponent& component) override {
			world.GetComponent<Engine::SimpleAnimationComponent>(entity) = component;
		}
		void OnAfterPreviewApplied(Engine::ECSWorld&, const Engine::Entity&, const Engine::SimpleAnimationComponent&) override {}
	};

	// 削除とUndo後に古い編集要求が復活しないことを確認する
	bool TestRemovedComponentEditSession() {

		using namespace Engine;
		ECSWorld world;
		const Entity entity = world.CreateEntity();
		world.AddComponent<SimpleAnimationComponent>(entity).playOnStart = false;
		EditorContext editor;
		editor.activeWorld = &world;
		EditorCommandContext commandContext;
		commandContext.editorContext = &editor;
		EditorPanelContext panel;
		panel.editorContext = &editor;
		SimpleAnimationEditHooks hooks;
		SerializedComponentEditSession<SimpleAnimationComponent> session("SimpleAnimation");
		session.SyncDraftFromWorld(world, entity, hooks);
		session.GetDraft().enabled = false;
		session.SetEditing(true);
		session.RequestCommit();

		RemoveComponentCommand remove(entity, "SimpleAnimation");
		if (!remove.Execute(commandContext) || world.HasComponent<SimpleAnimationComponent>(entity)) return false;
		session.SyncOwner(&world, entity);
		remove.Undo(commandContext);
		if (!world.HasComponent<SimpleAnimationComponent>(entity) || !session.NeedsSync(world, world.GetUUID(entity))) return false;
		// hostを使わず戻れることも古いCommit要求の破棄を検証する
		session.CommitIfNeeded(panel, world, entity, hooks);
		if (!world.GetComponent<SimpleAnimationComponent>(entity).enabled ||
			world.GetComponent<SimpleAnimationComponent>(entity).playOnStart) return false;
		if (!remove.Redo(commandContext) || world.HasComponent<SimpleAnimationComponent>(entity)) return false;
		session.SyncOwner(&world, entity);
		world.AddComponent<SimpleAnimationComponent>(entity);
		session.SyncDraftFromWorld(world, entity, hooks);
		if (!session.GetDraft().enabled || !session.GetDraft().playOnStart) return false;

		const Entity other = world.CreateEntity();
		world.AddComponent<SimpleAnimationComponent>(other);
		session.GetDraft().enabled = false;
		session.RequestCommit();
		session.SyncOwner(&world, other);
		session.SyncDraftFromWorld(world, other, hooks);
		session.CommitIfNeeded(panel, world, other, hooks);
		if (!world.GetComponent<SimpleAnimationComponent>(other).enabled) return false;
		editor.isPlaying = true;
		commandContext.allowRuntimeEdit = true;
		RemoveComponentCommand runtimeRemove(other, "SimpleAnimation");
		if (!runtimeRemove.Execute(commandContext)) return false;
		session.SyncOwner(&world, other);
		session.EndEditing();
		return !world.HasComponent<SimpleAnimationComponent>(other);
	}

	// サブメッシュ選択の所有元と編集コマンドの保存を確認する
	bool TestSubMeshSelection() {

		using namespace Engine;
		ECSWorld world;
		const Entity entity = world.CreateEntity();
		world.AddComponent<SceneObjectComponent>(entity);
		world.AddComponent<MeshRendererComponent>(entity);
		SubMeshMaterial initial;
		initial.stableID = Engine::UUID{123};
		SetMeshSubMeshes(world, entity, std::span(&initial, 1));
		EditorState state;
		state.SelectMeshSubMesh(entity, 0, initial.stableID);
		if (state.ResolveComponentSelection(&world) != entity) return false;
		nlohmann::json before;
		world.SerializeComponentToJson(entity, "MeshRenderer", before);
		auto edited = initial;
		edited.localPos.x = 3.0f;
		edited.localRotation.y = 30.0f;
		edited.localScale.z = 2.0f;
		edited.uvPos.x = 0.25f;
		edited.uvRotation = 0.5f;
		edited.uvScale.y = 3.0f;
		edited.materialInstance.Set(MaterialParameterNames::Roughness, MaterialParameterValue{.value = 0.25f});
		nlohmann::json after;
		SerializeMeshRenderer(world.GetComponent<MeshRendererComponent>(entity), std::span(&edited, 1), after);
		EditorContext editor;
		editor.activeWorld = &world;
		EditorCommandContext commands;
		commands.editorContext = &editor;
		commands.editorState = &state;
		SetSerializedComponentCommand command(entity, "MeshRenderer", before, after);
		if (!command.Execute(commands) || state.selectionKind != EditorSelectionKind::MeshSubMesh) return false;
		nlohmann::json actual;
		world.SerializeComponentToJson(entity, "MeshRenderer", actual);
		if (actual != after || state.ResolveComponentSelection(&world) != entity) return false;
		command.Undo(commands);
		world.SerializeComponentToJson(entity, "MeshRenderer", actual);
		if (actual != before || !command.Redo(commands)) return false;
		world.SerializeComponentToJson(entity, "MeshRenderer", actual);
		if (actual != after) return false;

		// 配列位置が変わっても安定IDから同じ所有元を解決する
		state.selectedSubMeshIndex = 8;
		if (!state.HasValidSubMeshSelection(&world) || state.ResolveComponentSelection(&world) != entity) return false;
		state.ValidateSelection(&world);
		if (state.selectedSubMeshIndex != 0) return false;
		state.SelectEntity(entity);
		if (state.ResolveComponentSelection(&world) != entity) return false;
		state.selectionKind = EditorSelectionKind::Joint;
		if (state.ResolveComponentSelection(&world) != Entity::Null()) return false;
		state.selectionKind = EditorSelectionKind::Asset;
		if (state.ResolveComponentSelection(&world) != Entity::Null()) return false;
		state.SelectMeshSubMesh(entity, 99);
		if (state.ResolveComponentSelection(&world) != Entity::Null()) return false;
		state.ClearSelection();
		return state.ResolveComponentSelection(&world) == Entity::Null();
	}

	bool TestColorEditCompletion() {

		using namespace Engine;
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* gui = ImGui::CreateContext();
		ScopedCleanup cleanup([=]() noexcept { ImGui::DestroyContext(gui); ImGui::SetCurrentContext(previous); });
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(800, 600);
		io.DeltaTime = 1.0f / 60.0f;
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		Color4 value(1.0f, 0.0f, 0.0f, 1.0f);
		ImVec2 origin;
		int completions = 0;
		int changes = 0;
		for (int frame = 0; frame < 14; ++frame) {
			if (frame > 0) io.AddMousePosEvent(origin.x + 230.0f, origin.y + 20.0f + float(frame) * 8.0f);
			if (frame == 2 || frame == 8) io.AddMouseButtonEvent(0, true);
			if (frame == 6 || frame == 12) io.AddMouseButtonEvent(0, false);
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(500, 550));
			ImGui::Begin("ColorCompletion", nullptr, ImGuiWindowFlags_NoTitleBar);
			origin = ImGui::GetCursorScreenPos();
			ImGui::SetNextItemWidth(240.0f);
			const auto result = MyGUI::ColorPicker("Picker", value, ImGuiColorEditFlags_PickerHueBar |
				ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoInputs);
			changes += result.valueChanged ? 1 : 0;
			completions += result.editFinished ? 1 : 0;
			Color3 other(0.2f, 0.3f, 0.4f);
			const auto unrelated = MyGUI::ColorEdit("Other", other);
			ImGui::End();
			ImGui::Render();
			if ((io.MouseDown[0] && result.editFinished) || unrelated.editFinished) return false;
			if (frame == 6 && completions != 1) return false;
		}
		if (completions != 2 || changes < 4) {
			std::cerr << "Color edit: changes=" << changes << " completions=" << completions << "\n";
			return false;
		}
		return true;
	}

	// 読み取り専用表示のコピーとIDと保存値の不変性を確認する
	bool TestReadOnlyFields() {

		using namespace Engine;
		ImGuiContext* previous = ImGui::GetCurrentContext();
		ImGuiContext* gui = ImGui::CreateContext();
		ScopedCleanup cleanup([=]() noexcept { ImGui::DestroyContext(gui); ImGui::SetCurrentContext(previous); });
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(900, 1600);
		io.DeltaTime = 1.0f / 60.0f;
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		std::string clipboard;
		auto& platform = ImGui::GetPlatformIO();
		platform.Platform_ClipboardUserData = &clipboard;
		platform.Platform_SetClipboardTextFn = [](ImGuiContext* context, const char* text) {
			*static_cast<std::string*>(context->PlatformIO.Platform_ClipboardUserData) = text;
		};
		using Kind = ManagedSerializedFieldKind;
		auto number = std::make_shared<ManagedFieldSchema>();
		number->kind = Kind::Double;
		auto nullable = std::make_shared<ManagedFieldSchema>();
		nullable->kind = Kind::Nullable;
		nullable->element = number;
		ManagedFieldSchema list;
		list.name = "Samples";
		list.kind = Kind::List;
		list.isReadOnly = true;
		list.element = nullable;
		nlohmann::json values = nlohmann::json::array({nullptr, 1.234567890123, -4.5});
		const auto saved = values;
		auto member = std::make_shared<ManagedFieldSchema>();
		member->name = "Color";
		member->kind = Kind::Color4;
		ManagedFieldSchema object;
		object.name = "Object";
		object.kind = Kind::Object;
		object.members = {member};
		nlohmann::json objectValue = {{"Color", {{"r", 2.0}, {"g", 3.0}, {"b", 4.0}, {"a", 0.5}}}};
		const auto savedObject = objectValue;
		ImVec2 click;
		ImVec2 longClick;
		bool changingValueCopied = false;
		ImGuiID initialID = 0;
		bool valid = true;
		auto check = [&](bool passed, const char* reason) {
			if (!passed) std::cerr << "Read-only display: " << reason << "\n";
			valid &= passed;
		};
		const std::string longText = std::string(300, 'W') + "##全文の末尾";
		for (int frame = 0; frame < 7; ++frame) {
			if (frame > 0) { const ImVec2 target = frame < 4 ? click : longClick; io.AddMousePosEvent(target.x, target.y); }
			if (frame == 2 || frame == 5) io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			if (frame == 3 || frame == 6) io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0, 0));
			ImGui::SetNextWindowSize(ImVec2(700, 1500));
			ImGui::Begin("ReadOnlyFields", nullptr, ImGuiWindowFlags_NoTitleBar);
			MyGUI::BeginPropertyRow("First");
			DrawCopyableValue("##Value", frame >= 3 ? "11" : "10", ImGui::GetContentRegionAvail().x);
			const ImGuiID first = gui->LastItemData.ID;
			if (frame == 0) { initialID = first; click = gui->LastItemData.Rect.GetCenter(); }
			check(first != 0 && first == initialID, "changing value ID");
			MyGUI::EndPropertyRow();
			MyGUI::BeginPropertyRow("Second");
			DrawCopyableValue("##Value", "10", ImGui::GetContentRegionAvail().x);
			check(gui->LastItemData.ID != first, "equal value IDs");
			MyGUI::EndPropertyRow();
			const float top = ImGui::GetCursorPosY();
			MyGUI::Text("Long", longText);
			if (frame < 4) longClick = gui->LastItemData.Rect.GetCenter();
			check(ImGui::GetCursorPosY() - top > ImGui::GetFrameHeight() * 2, "long text wrapping");
			MyGUI::TextInt("Int", INT32_MIN);
			MyGUI::TextUInt("UInt", UINT32_MAX);
			MyGUI::TextInt64("Int64", INT64_MIN);
			MyGUI::TextUInt64("UInt64", UINT64_MAX);
			MyGUI::TextDouble("Double", 1.234567890123, 12);
			MyGUI::TextBool("Bool", true);
			MyGUI::TextVector4("Vector", Vector4(1, 1, 1, 1));
			MyGUI::TextColor3("Color3", Color3(1, 1, 1));
			MyGUI::TextColor4("Color4", Color4(1, 1, 1, 1));
			MyGUI::TextMatrix4x4("Matrix", Matrix4x4::Identity());
			ScriptFieldInspector::DrawContext context;
			ImGui::SetNextItemOpen(true);
			const auto listResult = ScriptFieldInspector::DrawValue(list, values, context, "Samples");
			context.readOnly = true;
			ImGui::SetNextItemOpen(true);
			const auto objectResult = ScriptFieldInspector::DrawValue(object, objectValue, context, "Object");
			check(!listResult.valueChanged && !objectResult.valueChanged && values == saved && objectValue == savedObject, "read-only data changed");
			ImGui::PushID("Playback");
			check(DrawInspectorPlaybackControls(false) == InspectorPlaybackAction::None, "disabled playback controls");
			ImGui::PopID();
			check((gui->CurrentItemFlags & ImGuiItemFlags_Disabled) == 0, "disabled flag leak");
			ImGui::End();
			ImGui::Render();
			if (frame == 3) changingValueCopied = clipboard == "11";
		}
		check(changingValueCopied, "changing value clipboard");
		check(clipboard == longText, "long text clipboard");
		return valid;
	}

}

bool NEMTests::TestInspectorPresentation() {

	if (!CheckDebugDrawSettings<Engine::PerspectiveCameraComponent>() ||
		!CheckDebugDrawSettings<Engine::DirectionalLightComponent>() || !CheckDebugDrawSettings<Engine::PointLightComponent>() ||
		!CheckDebugDrawSettings<Engine::SpotLightComponent>() || !CheckDebugDrawSettings<Engine::RectLightComponent>()) return false;

	using namespace Engine;
	if (!TestRemovedComponentEditSession() || !TestSubMeshSelection() || !TestReadOnlyFields() || !TestColorEditCompletion()) return false;
	if (std::string_view(GetInspectorPlaybackState(true, false)) != "再生中" ||
		std::string_view(GetInspectorPlaybackState(true, true)) != "一時停止" ||
		std::string_view(GetInspectorPlaybackState(false, false, true)) != "完了" ||
		std::string_view(GetInspectorPlaybackState(false, false)) != "停止中" ||
		std::string_view(GetInspectorPlaybackState(true, false, false, true)) != "計測中" ||
		std::string_view(GetInspectorLabel(InspectorFieldID::PlaybackMode)) != "繰り返し" ||
		std::string_view(GetInspectorLabel(InspectorFieldID::Duration)) != "再生時間" ||
		std::string_view(GetInspectorLabel(InspectorFieldID::StartDelay)) != "開始待ち" ||
		std::string_view(GetInspectorLabel(InspectorFieldID::AxisAngle)) != "軸＋角度") return false;
	SkyAtmosphereComponent atmosphere;
	const auto zenith = AtmosphereLighting::SunTransmittance(atmosphere, 0.0f, Vector3(0, 1, 0));
	const auto elevated = AtmosphereLighting::SunTransmittance(atmosphere, 10.0f, Vector3(0, 1, 0));
	const auto horizon = AtmosphereLighting::SunTransmittance(atmosphere, 0.0f, Vector3(1, 0, 0));
	const auto below = AtmosphereLighting::SunTransmittance(atmosphere, 0.0f, Vector3(0, -1, 0));
	if (!(zenith.x > 0 && zenith.x < 1 && zenith.x >= zenith.y && zenith.y >= zenith.z &&
		elevated.x > zenith.x && elevated.y > zenith.y && elevated.z > zenith.z &&
		horizon.x < zenith.x && horizon.y < zenith.y && horizon.z < zenith.z &&
		below.x == 0 && below.y == 0 && below.z == 0)) return false;
	auto vacuum = atmosphere;
	vacuum.rayleighScattering = Color3::Black();
	vacuum.mieScattering = vacuum.mieAbsorption = 0;
	vacuum.ozoneAbsorption = Color3::Black();
	const auto clear = AtmosphereLighting::SunTransmittance(vacuum, 0.0f, Vector3(0, 1, 0));
	if (clear.x != 1 || clear.y != 1 || clear.z != 1) return false;
	auto invalidAtmosphere = atmosphere;
	invalidAtmosphere.planetRadiusKm = 0;
	invalidAtmosphere.rayleighScaleHeightKm = std::numeric_limits<float>::quiet_NaN();
	invalidAtmosphere.mieAbsorption = std::numeric_limits<float>::infinity();
	const auto sanitized = AtmosphereLighting::Sanitize(invalidAtmosphere);
	if (!(sanitized.planetRadiusKm > 0 && std::isfinite(sanitized.rayleighScaleHeightKm) &&
		sanitized.rayleighScaleHeightKm > 0 && std::isfinite(sanitized.mieAbsorption))) return false;
	ExponentialHeightFogComponent fog;
	atmosphere.mieAnisotropy = 0.7f;
	atmosphere.sunEntity.localFileID = Engine::UUID::New();
	fog.secondDensity = 0.04f;
	fog.secondHeightOffset = -3.0f;
	const nlohmann::json savedAtmosphere = atmosphere;
	const nlohmann::json savedFog = fog;
	const auto restoredAtmosphere = savedAtmosphere.get<SkyAtmosphereComponent>();
	const auto restoredFog = savedFog.get<ExponentialHeightFogComponent>();
	if (nlohmann::json(restoredAtmosphere) != savedAtmosphere || nlohmann::json(restoredFog) != savedFog) return false;
	if (BindInspectorCommonFields(atmosphere).enabled != &atmosphere.enabled ||
		BindInspectorCommonFields(fog).displayLayer != &fog.renderingLayerMask) return false;
	SkyboxRendererComponent sky;
	ParticleSystemComponent particle;
	ForceFieldComponent field;
	FixedJointComponent joint;
	PerspectiveCameraComponent camera;
	const auto skyFields = BindInspectorCommonFields(sky);
	const auto particleFields = BindInspectorCommonFields(particle);
	const auto fieldFields = BindInspectorCommonFields(field);
	const auto jointFields = BindInspectorCommonFields(joint);
	const auto cameraFields = BindInspectorCommonFields(camera);
	if (skyFields.enabled || skyFields.visible != &sky.visible || skyFields.displayLayer != &sky.renderingLayerMask ||
		particleFields.enabled != &particle.enabled || particleFields.visible != &particle.visible ||
		fieldFields.particleGroup != &field.layerMask || fieldFields.displayLayer || jointFields.enabled != &joint.enabled ||
		cameraFields.cameraLayer != &camera.common.cullingMask || cameraFields.enabled != &camera.common.enabled) return false;

	ComponentEditorRegistry registry;
	for (const auto& [name, category] : std::array{std::pair{"ForceField", "Physics"},
		std::pair{"MeshRenderer", "Rendering"}, std::pair{"FlipbookAnimation", "Rendering"},
		std::pair{"AnimationClipPlayer", "Animation"}, std::pair{"Timer", "Time"}, std::pair{"Spline", "Core"}}) {
		ComponentEditorDescriptor entry;
		entry.typeName = entry.menuLabel = name;
		entry.category = category;
		entry.drawerFactory = [] { return std::make_unique<EmptyDrawer>(); };
		registry.Register(std::move(entry));
	}
	std::unordered_set<std::string> types;
	int lastCategory = -1;
	for (const auto& entry : registry.GetDescriptors()) {
		const int order = GetInspectorCategoryOrder(entry.category);
		if (order < lastCategory || !types.insert(entry.typeName).second || !GetInspectorCategoryLabel(entry.category)) return false;
		lastCategory = order;
		if (entry.typeName == "AnimationClipPlayer" && entry.menuLabel != "AnimationClipPlayer") return false;
	}
	if (std::string_view(GetInspectorCategoryLabel("Core")) != "空間・経路" ||
		std::string_view(GetInspectorCategoryLabel("Time")) != "時間" || types.size() != 6 || GetInspectorComponentCategory("ForceField", "Physics") != "Particle" ||
		GetInspectorComponentCategory("FlipbookAnimation", "Rendering") != "Animation") return false;
	for (const auto& drawer : registry.GetDrawers()) if (drawer->GetDisplayName().empty() || drawer->GetComponentTypeName().empty()) return false;
	for (const char* name : EnumAdapter<EasingType>::GetEnumArray()) {
		if (std::string_view(ImGuiUtility::GetEnumDisplayName("EasingType", name)) != name) return false;
	}
	// 表示だけで上位ビットや参照切れを失わない
	ImGuiContext* previous = ImGui::GetCurrentContext();
	ImGuiContext* gui = ImGui::CreateContext();
	ScopedCleanup cleanup([=]() noexcept { ImGui::DestroyContext(gui); ImGui::SetCurrentContext(previous); });
	auto& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1024, 900);
	io.DeltaTime = 1.0f / 60.0f;
	unsigned char* pixels;
	int width, height;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
	EditorPanelContext context;
	ProjectRenderingLayerSettings layers;
	context.renderingLayerSettings = &layers;
	ECSWorld world;
	const auto owner = world.CreateEntity();
	auto& source = world.AddComponent<SceneObjectComponent>(owner);
	source.localFileID = world.GetUUID(owner);
	SceneEntityReference missing{ {}, Engine::UUID::New() };
	const auto savedMissing = missing;
	Engine::UUID missingLocal = missing.localFileID;
	uint32_t mask = 0xC0000001u;
	field.layerMask = mask;
	EasingType easing = EasingType::EaseInOutBounce;
	for (int frame = 0; frame < 3; ++frame) {
		ImGui::NewFrame();
		ImGui::SetNextWindowSize(ImVec2(800, 850));
		ImGui::Begin("InspectorPresentation");
		const auto indent = ImGui::GetCursorPosX();
		{ InspectorIndentScope nested; MyGUI::CollapsingHeader("ClosedGroup", false); }
		const bool indentationRestored = ImGui::GetCursorPosX() == indent;
		DrawInspectorCommonFields(context, fieldFields);
		DrawInspectorMask(context, "Rendering", mask, InspectorMaskDomain::Rendering);
		DrawInspectorMask(context, "Particle", mask, InspectorMaskDomain::Particle);
		DrawInspectorMask(context, "Collision", mask, InspectorMaskDomain::Collision);
		SceneEntityReferenceFields::Draw("Missing", world, owner, missing);
		SceneEntityReferenceFields::DrawLocal("Local", world, missingLocal);
		Engine::UUID empty;
		MyGUI::EntityReferenceField("Empty", empty, &world);
		MyGUI::EnumCombo("Easing", easing);
		ImGui::End();
		ImGui::Render();
		if (!indentationRestored) return false;
	}
	if (mask != 0xC0000001u || field.layerMask != mask || missing.localFileID != savedMissing.localFileID ||
		missingLocal != savedMissing.localFileID || easing != EasingType::EaseInOutBounce) return false;
	std::cout << "Inspector presentation: bindings, categories, masks, references and easing passed\n";
	return true;
}
