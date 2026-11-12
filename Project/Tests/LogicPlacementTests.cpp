#include "LogicPlacementTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Geometry/LogicPlacement.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Editor/Commands/Entity/PlaceObjectsCommand.h>
#include <Engine/Editor/Utility/LogicPlacementSpline.h>
#include <Engine/Editor/Utility/LogicPlacementPreview.h>
#include <Engine/Editor/UI/Common/LogicPlacementFields.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_set>
#include <stdexcept>

using namespace Engine;

namespace {

	// 配置途中に一度だけ生成通知を失敗させる
	void FailPlacement(ECSWorld&, const Entity&, uint32_t, ComponentMutationKind kind, void* user) {

		auto& remaining = *static_cast<int32_t*>(user);
		if (kind == ComponentMutationKind::EntityCreated && remaining >= 0 && remaining-- == 0)
			throw std::runtime_error("配置途中の失敗を検証");
	}

	// 外周と内部の個数、平面、乱数の再現性を確かめる
	bool TestGeometry() {

		LogicPlacementSettings settings;
		settings.fill = true;
		settings.interiorCount = 37;
		auto circle = BuildLogicPlacement(settings);
		if (!circle.IsValid() || circle.boundaryCount != 8 || circle.points.size() != 17 || circle.lines.size() != 192 ||
			circle.points.back().position != Vector3{}) return false;
		for (size_t i = 0; i < circle.points.size(); ++i) {
			const auto point = circle.points[i].position;
			if (point.y != 0 || (i < 8 ? std::abs(point.Length() - 1) > 0.00001f : point.Length() >= 1)) return false;
		}
		settings.distribution = LogicPlacementDistribution::Random;
		settings.seed = 42;
		const auto first = BuildLogicPlacement(settings);
		const auto second = BuildLogicPlacement(settings);
		for (size_t i = 0; i < first.points.size(); ++i) if (first.points[i].position != second.points[i].position) return false;
		++settings.seed;
		if (BuildLogicPlacement(settings).points.back().position == first.points.back().position) return false;
		settings.shape = LogicPlacementShape::Polygon;
		settings.vertices = 5;
		settings.subdivisions = 3;
		for (const auto distribution : {LogicPlacementDistribution::Uniform, LogicPlacementDistribution::Random}) {
			settings.distribution = distribution;
			const auto polygon = BuildLogicPlacement(settings);
			const size_t expected = distribution == LogicPlacementDistribution::Uniform ? 31 : 52;
			if (!polygon.IsValid() || polygon.boundaryCount != 15 || polygon.points.size() != expected) return false;
			for (size_t i = polygon.boundaryCount; i < polygon.points.size(); ++i) for (int32_t edge = 0; edge < settings.vertices; ++edge) {
				const auto& line = polygon.lines[static_cast<size_t>(edge)];
				const auto a = line.to - line.from;
				const auto b = polygon.points[i].position - line.from;
				if (a.x * b.z - a.z * b.x < -0.000001f) return false;
			}
		}
		settings.fill = false;
		settings.is2D = true;
		const auto planar = BuildLogicPlacement(settings);
		for (const auto& point : planar.points) if (point.position.z != 0) return false;
		settings.is2D = false;
		settings.plane = LogicPlacementPlane::YZ;
		for (const auto& point : BuildLogicPlacement(settings).points) if (point.position.x != 0) return false;
		settings.shape = LogicPlacementShape::Line;
		settings.count = 4;
		settings.length = 6;
		const auto line = BuildLogicPlacement(settings);
		if (line.points.size() != 4 || std::abs((line.points.back().position - line.points.front().position).Length() - 6) > 0.00001f) return false;
		settings.shape = LogicPlacementShape::Grid;
		settings.columns = 3;
		settings.rows = 4;
		settings.layers = 5;
		if (BuildLogicPlacement(settings).points.size() != 60) return false;
		settings.is2D = true;
		if (BuildLogicPlacement(settings).points.size() != 12) return false;
		settings.columns = INT32_MAX;
		settings.rows = INT32_MAX;
		settings.layers = INT32_MAX;
		if (BuildLogicPlacement(settings).IsValid()) return false;
		settings = {};
		settings.radius = std::numeric_limits<float>::infinity();
		if (BuildLogicPlacement(settings).IsValid()) return false;
		settings = {};
		const nlohmann::json saved = settings;
		return BuildLogicPlacement(saved.get<LogicPlacementSettings>()).IsValid();
	}

	// 四角形の各段と中心が等間隔の格子へ揃うことを確かめる
	bool TestUniformLayers() {

		LogicPlacementSettings settings;
		settings.shape = LogicPlacementShape::Polygon;
		settings.vertices = 4;
		settings.subdivisions = 6;
		settings.radius = std::sqrt(18.0f);
		settings.startAngle = 45;
		settings.fill = true;
		settings.interiorLayers = 2;
		for (const bool is2D : {false, true}) {
			settings.is2D = is2D;
			const auto result = BuildLogicPlacement(settings);
			if (!result.IsValid() || result.boundaryCount != 24 || result.points.size() != 49 || result.lines.size() != 12) return false;
			std::array<int32_t, 4> layers{};
			for (const auto& point : result.points) {
				const float v = is2D ? point.position.y : point.position.z;
				const float layer = (std::max)(std::abs(point.position.x), std::abs(v));
				const auto index = static_cast<int32_t>(std::round(layer));
				if (index < 0 || index > 3 || std::abs(layer - static_cast<float>(index)) > 0.00001f ||
					std::abs(point.position.x - std::round(point.position.x)) > 0.00001f ||
					std::abs(v - std::round(v)) > 0.00001f) return false;
				++layers[static_cast<size_t>(index)];
			}
			if (layers != std::array<int32_t, 4>{1, 8, 16, 24}) return false;
		}
		settings.interiorLayers = 0;
		if (BuildLogicPlacement(settings).points.size() != 25) return false;
		settings.interiorLayers = -1;
		if (BuildLogicPlacement(settings).IsValid()) return false;
		settings.interiorLayers = INT32_MAX;
		return !BuildLogicPlacement(settings).IsValid();
	}

	// 接線付き生成と保存互換を確かめる
	bool TestSpline() {

		LogicPlacementSettings settings;
		SplineData data;
		if (!ApplyLogicPlacementSpline(settings, BuildLogicPlacement(settings), data) || !data.closed || data.points.size() != 8) return false;
		for (const auto& sample : BuildSplineSamples(data, Matrix4x4::Identity()))
			if (std::abs(sample.position.Length() - 1) > 0.001f) return false;
		settings.shape = LogicPlacementShape::Polygon;
		settings.vertices = 4;
		settings.subdivisions = 2;
		settings.is2D = true;
		if (!ApplyLogicPlacementSpline(settings, BuildLogicPlacement(settings), data) || !data.closed || !data.is2D) return false;
		for (const auto& point : data.points) if (point.position.z != 0 || point.tangentIn.z != 0 || point.tangentOut.z != 0) return false;
		settings.shape = LogicPlacementShape::Line;
		if (!ApplyLogicPlacementSpline(settings, BuildLogicPlacement(settings), data) || data.closed) return false;
		const nlohmann::json saved = data;
		settings.shape = LogicPlacementShape::Grid;
		if (ApplyLogicPlacementSpline(settings, BuildLogicPlacement(settings), data)) return false;
		return nlohmann::json(data) == saved;
	}

	// 複製した階層の参照と一括Undoを確かめる
	bool TestCommand() {

		if (NameComponent{}.name != "GameObject" || nlohmann::json::object().get<NameComponent>().name != "GameObject") return false;
		ECSWorld world;
		EditorState state;
		EditorContext editor;
		SceneInstanceManager scenes;
		editor.activeWorld = &world;
		editor.sceneInstances = &scenes;
		editor.activeSceneInstanceID = scenes.CreateScratchScene({});
		EditorCommandContext context;
		context.editorContext = &editor;
		context.editorState = &state;
		const auto source = SceneAuthoring::CreateGameObject(world, "Source");
		const auto child = SceneAuthoring::CreateGameObject(world, "Child");
		HierarchySystem hierarchy;
		hierarchy.SetParent(world, child, source);
		world.GetComponent<SceneObjectComponent>(source).sceneInstanceID = editor.activeSceneInstanceID;
		world.GetComponent<SceneObjectComponent>(child).sceneInstanceID = editor.activeSceneInstanceID;
		auto& light = world.AddComponent<PointLightComponent>(source);
		light.influence.shadowExclusions.push_back({SceneEntityReference::FromEntity(world, child), true});
		world.GetComponent<TransformComponent>(source).localScale = {2, 3, 4};
		state.SelectEntity(source);
		LogicPlacementRequest request;
		request.source.object = world.GetUUID(source);
		request.parentName = " \t";
		request.parentTransform.localPos = {3, 4, 5};
		request.parentTransform.localScale = {2, 2, 2};
		request.settings.count = 3;
		PlaceObjectsCommand command(request);
		if (!command.Execute(context)) return false;
		const auto parent = state.selectedEntity;
		const auto parentID = world.GetUUID(parent);
		const auto generated = HierarchyUtility::CollectLogicalSubtree(world, parent);
		if (generated.size() != 7 || world.GetComponent<NameComponent>(parent).name != "Object" ||
			world.GetComponent<TransformComponent>(parent).localPos != request.parentTransform.localPos) return false;
		std::unordered_set<Engine::UUID> ids;
		for (const auto object : generated) {
			ids.insert(world.GetUUID(object));
			if (const auto* value = world.TryGetComponent<PointLightComponent>(object)) {
				const auto target = ResolveLightShadowExclusion(world, object, value->influence.shadowExclusions.front());
				if (!world.IsAlive(target) || target == child || HierarchyUtility::GetParent(world, target) != object) return false;
				if (world.GetComponent<TransformComponent>(object).localScale != Vector3(2, 3, 4)) return false;
			}
		}
		command.Undo(context);
		if (world.IsAlive(world.FindByUUID(parentID)) || state.selectedEntity != source) return false;
		world.GetComponent<TransformComponent>(source).localScale = {10, 10, 10};
		if (!command.Redo(context) || world.GetUUID(state.selectedEntity) != parentID) return false;
		for (const auto id : ids) if (!world.IsAlive(world.FindByUUID(id))) return false;
		command.Undo(context);
		int32_t remaining = 3;
		const auto listener = world.AddComponentMutationListener(&FailPlacement, &remaining);
		PlaceObjectsCommand failed(request);
		bool threw = false;
		try { failed.Execute(context); } catch (const std::runtime_error&) { threw = true; }
		world.RemoveComponentMutationListener(listener);
		size_t alive = 0;
		world.ForEachAliveEntity([&](Entity) { ++alive; });
		if (!threw || alive != 2 || state.selectedEntity != source) return false;
		request.settings.is2D = true;
		PlaceObjectsCommand mismatch(request);
		if (mismatch.Execute(context)) return false;
		request.source = {};
		PlaceObjectsCommand empty(request);
		return !empty.Execute(context);
	}

	// PrefabのリンクとRedo時の生成結果を確かめる
	bool TestPrefab() {

		NEMTests::TestDirectory directory("LogicPlacement", RuntimePaths::GetGameAssetsRoot());
		const auto path = directory.GetPath() / "Source.prefab.json";
		TransformComponent transform;
		transform.dimension = Dimension::Type2D;
		nlohmann::json document{{"SchemaVersion", 2}, {"Header", {{"rootLocalFileID", "0000000000000001"}}},
			{"Entities", nlohmann::json::array({{{"LocalFileID", "0000000000000001"},
				{"Components", {{"Name", {{"name", "Source"}}}, {"Transform", transform}}}}})}};
		if (!JsonFile::Save(path, document)) return false;
		AssetDatabase database;
		database.Init();
		const auto asset = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::Prefab);
		ECSWorld world;
		EditorState state;
		EditorContext editor;
		editor.activeWorld = &world;
		editor.assetDatabase = &database;
		EditorCommandContext context;
		context.editorContext = &editor;
		context.editorState = &state;
		LogicPlacementRequest request;
		request.source.prefab = asset;
		request.settings.is2D = true;
		request.settings.shape = LogicPlacementShape::Line;
		request.settings.count = 2;
		PlaceObjectsCommand command(request);
		if (!asset || !command.Execute(context)) return false;
		const auto id = world.GetUUID(state.selectedEntity);
		std::unordered_set<Engine::UUID> instances;
		world.ForEach<PrefabLinkComponent>([&](Entity, PrefabLinkComponent& link) { instances.insert(link.prefabInstanceID); });
		if (instances.size() != 2 || instances.contains(Engine::UUID{})) return false;
		command.Undo(context);
		document["Entities"][0]["Components"]["Transform"] = 123;
		if (!JsonFile::Save(path, document) || !command.Redo(context)) return false;
		std::unordered_set<Engine::UUID> restored;
		world.ForEach<PrefabLinkComponent>([&](Entity, PrefabLinkComponent& link) { restored.insert(link.prefabInstanceID); });
		return restored == instances && world.GetUUID(state.selectedEntity) == id;
	}

	// UIの描画とWorldを跨がないプレビューを確かめる
	bool TestPresentation() {

		const auto* previous = ImGui::GetCurrentContext();
		auto* gui = ImGui::CreateContext();
		ScopedCleanup cleanup([=]() noexcept {
			ImGui::DestroyContext(gui);
			ImGui::SetCurrentContext(const_cast<ImGuiContext*>(previous));
		});
		auto& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = {1000, 900};
		io.DeltaTime = 1.0f / 60;
		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		ECSWorld world;
		ECSWorld other;
		EditorState state;
		EditorContext editor;
		editor.activeWorld = &world;
		EditorPanelContext context;
		context.editorContext = &editor;
		context.editorState = &state;
		ResolvedRenderView view;
		view.orthographic.valid = view.perspective.valid = true;
		context.sceneRenderView = &view;
		LogicPlacementRequest request;
		for (int32_t frame = 0; frame < 4; ++frame) {
			request.settings.shape = static_cast<LogicPlacementShape>(frame);
			ImGui::NewFrame();
			ImGui::SetNextWindowSize({900, 850});
			ImGui::Begin("LogicPlacementPresentation");
			MyGUI::ScopedPropertyLabelWidth labels("LogicPlacementPresentation");
			LogicPlacementFields::DrawShape(request.settings);
			LogicPlacementFields::DrawOrientation(request);
			MyGUI::ObjectOrPrefabReferenceField("配置オブジェクト", request.source.object, request.source.prefab, &world, nullptr);
			const auto layout = std::make_shared<LogicPlacementResult>(BuildLogicPlacement(request.settings));
			SubmitLogicPlacementPreview(context, layout, Matrix4x4::MakeAffineMatrix(
				{0.2f, 0.2f, 0.2f}, Quaternion::Identity(), {0, 0, 0.5f}), false);
			auto* draw = ImGui::GetWindowDrawList();
			const int32_t before = draw->VtxBuffer.Size;
			DrawLogicPlacementPreview(context, {20, 20}, {400, 400});
			if (draw->VtxBuffer.Size <= before) return false;
			const int32_t drawn = draw->VtxBuffer.Size;
			editor.activeWorld = &other;
			DrawLogicPlacementPreview(context, {20, 20}, {400, 400});
			editor.activeWorld = &world;
			state.logicPlacementPreviews[0].requestedFrame = -100;
			DrawLogicPlacementPreview(context, {20, 20}, {400, 400});
			if (draw->VtxBuffer.Size != drawn) return false;
			auto clipped = std::make_shared<LogicPlacementResult>();
			clipped->points.push_back({{0, 0, -1}, {}});
			clipped->lines.push_back({{-0.5f, 0, -1}, {0.5f, 0, 0.5f}});
			SubmitLogicPlacementPreview(context, clipped, Matrix4x4::Identity(), false);
			DrawLogicPlacementPreview(context, {20, 20}, {400, 400});
			if (draw->VtxBuffer.Size <= drawn) return false;
			ImGui::End();
			ImGui::Render();
		}
		return true;
	}
}

bool NEMTests::TestLogicPlacement() {

	for (const auto& [name, test] : std::array<std::pair<const char*, bool(*)()>, 6>{{
		{"geometry", TestGeometry}, {"uniform layers", TestUniformLayers}, {"spline", TestSpline},
		{"command", TestCommand}, {"prefab", TestPrefab}, {"UI", TestPresentation}}}) {
		if (!test()) { std::cerr << "LogicPlacement: " << name << " failed\n"; return false; }
	}
	return true;
}
