#include "TestContracts.h"
#include "TestFixtures.h"

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSemanticMerge.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/DimensionType.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Runtime/Packages/PackageResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace NEMTests {

	bool TestAssetGUIDRoundTrip() {

		constexpr std::string_view source = "d0d59331ff0a4eb589ea7801cb52f208";
		const std::optional<Engine::AssetGUID> parsed = Engine::TryParseAssetGUID32Hex(source);
		return parsed && Engine::ToString(*parsed) == source;
	}

	bool TestContentHash() {

		const std::array<uint8_t, 3> bytes = { 'a', 'b', 'c' };
		return Engine::ContentHash::SHA256(bytes) ==
			"ba7816bf8f01cfea414140de5dae2223"
			"b00361a396177a9cb410ff61f20015ad";
	}

	bool TestPackageResolver() {

		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / "NEMEngineTests/PackageResolver";
		std::error_code ec;
		const std::filesystem::path normalizedRoot =
			std::filesystem::weakly_canonical(root.parent_path(), ec) / root.filename();
		if (normalizedRoot.parent_path() !=
			std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(), ec) /
			"NEMEngineTests") {
			return false;
		}
		std::filesystem::remove_all(normalizedRoot, ec);
		std::filesystem::create_directories(normalizedRoot / "Packages/com.nem.test", ec);
		if (ec) {
			return false;
		}

		{
			std::ofstream file(normalizedRoot / "Packages/manifest.json", std::ios::binary);
			file << R"({
  "schemaVersion": 1,
  "dependencies": {
    "com.nem.test": "1.0.0"
  }
})";
		}
		{
			std::ofstream file(normalizedRoot / "Packages/com.nem.test/package.json", std::ios::binary);
			file << R"({
  "name": "com.nem.test",
  "version": "1.0.0"
})";
		}
		{
			std::ofstream file(normalizedRoot / "Packages/com.nem.test/data.txt", std::ios::binary);
			file << "package content";
		}

		const Engine::PackageResolveResult result = Engine::PackageResolver::Resolve(
			normalizedRoot, normalizedRoot / "Packages", normalizedRoot / "Library");
		const bool passed = result.Succeeded() && result.packages.size() == 1 &&
			result.packages.front().name == "com.nem.test" &&
			result.packages.front().version == "1.0.0" &&
			result.packages.front().contentHash != 0 &&
			std::filesystem::exists(normalizedRoot / "Packages/packages-lock.json");
		std::filesystem::remove_all(normalizedRoot, ec);
		return passed;
	}

	bool TestVirtualPath() {

		Engine::RuntimePaths::Refresh();
		const std::filesystem::path gamePath =
			Engine::RuntimePaths::ResolveVirtualPath("game://Scenes/sampleScene.scene.json");
		return gamePath == (Engine::RuntimePaths::GetGameAssetsRoot() /
			"Scenes/sampleScene.scene.json").lexically_normal() &&
			Engine::RuntimePaths::ResolveVirtualPath("game://../ProjectSettings").empty();
	}

	bool TestJsonSemanticMerge() {

		const nlohmann::json base = {
			{ "Entities", {
				{
					{ "LocalFileID", "0000000000000001" },
					{ "Components", { { "Transform", { { "x", 0 }, { "y", 0 } } } } },
				},
				{
					{ "LocalFileID", "0000000000000002" },
					{ "Components", { { "Transform", { { "x", 0 }, { "y", 0 } } } } },
				},
			} },
		};
		nlohmann::json ours = base;
		nlohmann::json theirs = base;
		ours["Entities"][0]["Components"]["Transform"]["x"] = 10;
		theirs["Entities"][1]["Components"]["Transform"]["y"] = 20;

		const Engine::JsonMergeResult merged =
			Engine::JsonSemanticMerge::Merge(base, ours, theirs);
		if (!merged.Succeeded() ||
			merged.merged["Entities"][0]["Components"]["Transform"]["x"] != 10 ||
			merged.merged["Entities"][1]["Components"]["Transform"]["y"] != 20) {
			return false;
		}

		theirs["Entities"][0]["Components"]["Transform"]["x"] = 30;
		const Engine::JsonMergeResult conflicted =
			Engine::JsonSemanticMerge::Merge(base, ours, theirs);
		return conflicted.conflicts.size() == 1 &&
			conflicted.conflicts.front().path ==
			"/Entities/0000000000000001/Components/Transform/x";
	}

	bool TestTransformDimensionSerialization() {

		Engine::TransformComponent source{};
		source.dimension = Engine::Dimension::Type2D;

		nlohmann::json serialized{};
		Engine::to_json(serialized, source);
		Engine::TransformComponent restored{};
		Engine::from_json(serialized, restored);

		nlohmann::json legacy = serialized;
		legacy.erase("dimension");
		Engine::TransformComponent legacyRestored{};
		Engine::from_json(legacy, legacyRestored);

		return serialized.value("dimension", -1) ==
			static_cast<int>(Engine::Dimension::Type2D) &&
			restored.dimension == Engine::Dimension::Type2D &&
			legacyRestored.dimension == Engine::Dimension::Type3D;
	}

	bool TestUTF8Path() {

		const std::string directoryName =
			Engine::Algorithm::ConvertString(L"NEMEngineTests_日本語");
		const std::filesystem::path testRoot = std::filesystem::temp_directory_path() /
			Engine::Algorithm::PathFromUTF8(directoryName);
		const std::filesystem::path texturePath = testRoot / L"normalBlock.png";

		std::error_code ec;
		std::filesystem::remove_all(testRoot, ec);
		ec.clear();
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}
		{
			std::ofstream texture(texturePath, std::ios::binary);
			texture << "PNG";
		}

		const std::string serializedPath = Engine::Algorithm::PathToUTF8(texturePath);
		const std::filesystem::path restoredPath =
			Engine::Algorithm::PathFromUTF8(serializedPath);
		const bool passed = std::filesystem::exists(restoredPath, ec) && !ec &&
			Engine::Algorithm::PathToUTF8(testRoot.filename()) == directoryName &&
			Engine::AssetTypeResolver::GuessByPath(restoredPath) == Engine::AssetType::Texture;
		std::filesystem::remove_all(testRoot, ec);
		return passed;
	}

	bool TestRayTracingPipelineSerialization() {

		const nlohmann::json source = {
			{ "name", "RayTracingTest" },
			{ "variants", nlohmann::json::array({ {
				{ "kind", "Raytracing" },
				{ "shader", "4e454d4153534554da1b7b9bf8074052" },
				{ "rayGenerationExports", { "RayGen" } },
				{ "missExports", { "Miss" } },
				{ "hitGroups", nlohmann::json::array({ {
					{ "exportName", "HitGroup" },
					{ "closestHitExport", "ClosestHit" },
					{ "kind", "Triangles" },
				} }) },
				{ "staticSamplers", nlohmann::json::array({ {
					{ "shaderRegister", 0 },
				} }) },
			} }) },
		};
		Engine::RenderPipelineAsset pipeline{};
		if (!Engine::FromJson(source, pipeline) ||
			pipeline.variants.size() != 1 ||
			pipeline.variants.front().staticSamplers.size() != 1 ||
			pipeline.variants.front().staticSamplers.front().ShaderVisibility !=
				D3D12_SHADER_VISIBILITY_ALL) {

			return false;
		}

		const nlohmann::json serialized = Engine::ToJson(pipeline);
		return serialized["variants"][0]["rayGenerationExports"][0] ==
			"RayGen" &&
			serialized["variants"][0]["staticSamplers"][0]
				["shaderVisibility"] == "D3D12_SHADER_VISIBILITY_ALL";
	}

	bool TestCanvasNavigationTable() {

		Engine::ManagedNativeEntity firstGenerationEntity{};
		firstGenerationEntity.world = Engine::ManagedWorldHandle{ 0, 1 };
		firstGenerationEntity.index = 0;
		firstGenerationEntity.generation = 0;
		if (!firstGenerationEntity.IsValid()) {
			return false;
		}

		Engine::CanvasNavigationTable draft{};
		const Engine::UUID first = Engine::UUID::New();
		const Engine::UUID second = Engine::UUID::New();
		if (!Engine::SetCanvasNavigationCell(draft, 0, first) ||
			!Engine::SetCanvasNavigationCell(draft, 1, second) ||
			!Engine::ResizeCanvasNavigationTable(draft, 20, 20) ||
			draft.rows != 20 || draft.columns != 20 ||
			draft.cells.size() != 400 || draft.cells[0] != first || draft.cells[1] != second) {
			return false;
		}
		if (!Engine::SetCanvasNavigationCell(draft, 399, first) ||
			draft.cells[0] || draft.cells[399] != first ||
			Engine::ResizeCanvasNavigationTable(draft, 0, 20)) {
			return false;
		}

		Engine::ECSWorld world;
		Engine::HierarchySystem hierarchySystem;
		const Engine::Entity canvas =
			Engine::SceneAuthoring::CreateGameObject(world, "Canvas");
		const Engine::Entity selectable =
			Engine::SceneAuthoring::CreateGameObject(world, "Selectable");
		const Engine::Entity outside =
			Engine::SceneAuthoring::CreateGameObject(world, "Outside");
		world.AddComponent<Engine::CanvasComponent>(canvas);
		world.AddComponent<Engine::UISelectableComponent>(selectable);
		world.AddComponent<Engine::UISelectableComponent>(outside);
		hierarchySystem.SetParent(world, selectable, canvas);

		if (Engine::ResizeCanvasNavigationTable(world, canvas, 20, 20) !=
			Engine::CanvasNavigationTableResult::Success ||
			Engine::SetCanvasNavigationCell(world, canvas, 19, 19, selectable) !=
			Engine::CanvasNavigationTableResult::Success ||
			Engine::SetCanvasNavigationCell(world, canvas, 0, 0, outside) !=
			Engine::CanvasNavigationTableResult::InvalidTarget) {
			return false;
		}

		Engine::Entity resolved = Engine::Entity::Null();
		if (Engine::GetCanvasNavigationCell(world, canvas, 19, 19, resolved) !=
			Engine::CanvasNavigationTableResult::Success || resolved != selectable) {
			return false;
		}

		nlohmann::json canvasJson;
		Engine::CanvasComponent::SerializeECS(
			world, canvas, world.GetComponent<Engine::CanvasComponent>(canvas), canvasJson);
		if (canvasJson["navigationTable"].value("rows", 0) != 20 ||
			canvasJson["navigationTable"].value("columns", 0) != 20 ||
			canvasJson["navigationTable"]["cells"].size() != 400) {
			return false;
		}

		Engine::UISelectableComponent selectableSettings{};
		Engine::from_json(nlohmann::json{
			{ "normal",{
				{ "animationEnabled",false },
				{ "overrideTexture",true }
			} }
			}, selectableSettings);
		if (selectableSettings.normal.animationEnabled ||
			!selectableSettings.normal.overrideTexture ||
			!selectableSettings.selected.animationEnabled) {
			return false;
		}
		const nlohmann::json selectableJson = selectableSettings;
		return !selectableJson["normal"].value("animationEnabled", true) &&
			selectableJson["selected"].value("animationEnabled", false);
	}
}
