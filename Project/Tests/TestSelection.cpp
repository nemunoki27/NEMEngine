#include "TestSelection.h"

//============================================================================
//	include
//============================================================================
#include "TestRunner.h"
#include "GPUCompatibilityTests.h"
#include "TestContracts.h"
#include "ApplicationPlatformTests.h"
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "AudioHardwareTests.h"
#include "SceneStorageTests.h"
#include "FBXImportTests.h"
#include "EditorAssetWorkflowTests.h"
#include "ProjectGitIgnoreTests.h"
#include "GlobalIlluminationTests.h"
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>

// c++
#include <fstream>
#include <iostream>
#include <string_view>

using namespace NEMTests;

// 引数で指定された検証だけを実行する
std::optional<int> NEMTests::RunSelectedTests(int argc, char* argv[]) {

	if (1 < argc && std::string_view(argv[1]) == "--global-illumination-hardware") {
		return RunTest("TestGlobalIlluminationHardware", TestGlobalIlluminationHardware) ? 0 : 55;
	}
	if (1 < argc && std::string_view(argv[1]) == "--global-illumination") {
		return RunTest("TestGlobalIllumination", TestGlobalIllumination) ? 0 : 55;
	}
	if (1 < argc && std::string_view(argv[1]) == "--project-git-ignore") {
		return RunTest("TestProjectGitIgnoreDocument", TestProjectGitIgnoreDocument) ? 0 : 54;
	}
	if (1 < argc && std::string_view(argv[1]) == "--project-git-status") {
		return RunTest("TestProjectGitIgnoreReadOnly", TestProjectGitIgnoreReadOnly) ? 0 : 54;
	}
	if (1 < argc && std::string_view(argv[1]) == "--bistro-normals") {
		return RunTest("TestBistroNormalTextures", TestBistroNormalTextures) ? 0 : 53;
	}
	if (1 < argc && std::string_view(argv[1]) == "--editor-asset-workflow") {
		return RunTest("TestEditorAssetWorkflow", TestEditorAssetWorkflow) ? 0 : 53;
	}
	if (1 < argc && std::string_view(argv[1]) == "--fbx-import") {
		return RunTest("TestFBXImport", TestFBXImport) ? 0 : 52;
	}
	if (1 < argc && std::string_view(argv[1]) == "--gpu-compatible-hardware") {
		return RunTest("TestGPUCompatibility", TestGPUCompatibility) ? 0 : 47;
	}
	if (1 < argc && std::string_view(argv[1]) == "--audio-hardware") {
		return RunTest("TestAudioHardware", TestAudioHardware) ? 0 : 46;
	}

	if (1 < argc && std::string_view(argv[1]) == "--managed-build-diagnostics") {
		return RunTest("TestManagedBuildDiagnostics", TestManagedBuildDiagnostics) ? 0 : 51;
	}

	if (1 < argc && std::string_view(argv[1]) == "--managed-lifecycle") {
		return RunTest("TestManagedLifecycleIntegration", TestManagedLifecycleIntegration) ? 0 : 45;
	}

	if (1 < argc &&
		(std::string_view(argv[1]) == "--gpu-retirement" || std::string_view(argv[1]) == "--gpu-retirement-hardware")) {
		if (!TestGPURetirement(std::string_view(argv[1]) == "--gpu-retirement-hardware")) {
			return 44;
		}
		std::cout << "GPU retirement tests passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--editor") {
		if (!RunTest("TestEditorContracts", TestEditorContracts)) {
			return 43;
		}
		std::cout << "Editor tests passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--gameplay") {
		if (!RunTest("TestGameplayContracts", TestGameplayContracts)) {
			return 42;
		}
		std::cout << "Gameplay tests passed\n";
		return 0;
	}

	if (1 < argc && std::string_view(argv[1]) == "--foundation") {
		if (!RunTest("TestFoundationContracts", TestFoundationContracts)) {
			return 41;
		}
		std::cout << "Foundation tests passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--content-hash") {
		return RunTest("TestContentHash", TestContentHash) ? 0 : 48;
	}
	if (1 < argc && std::string_view(argv[1]) == "--ecs-structure") {
		return RunTest("TestECSStructureSafety", TestECSStructureSafety) ? 0 : 49;
	}
	if (1 < argc && std::string_view(argv[1]) == "--asset-watcher") {
		return RunTest("TestAssetWatcherLifetime", TestAssetWatcherLifetime) ? 0 : 50;
	}

	if (1 < argc && std::string_view(argv[1]) == "--scene-storage") {
		if (!RunTest("TestSceneAssetStorage", TestSceneAssetStorage)) {
			return 40;
		}
		std::cout << "Scene storage tests passed\n";
		return 0;
	}

	// 実シーンの互換復旧をファイル変更なしで検証する
	if (2 < argc && std::string_view(argv[1]) == "--prefab-recovery") {
		std::ifstream file(Engine::Algorithm::PathFromUTF8(argv[2]));
		auto scene = nlohmann::json::parse(file, nullptr, false);
		std::string diagnostic;
		if (!scene.is_object() || !Engine::PrefabReferenceRemapper::NormalizeLegacySceneInstances(scene, {}, diagnostic)) {
			std::cerr << "Prefab recovery failed: " << diagnostic << '\n';
			return 39;
		}
		for (const auto& instance : scene.value("PrefabInstances", nlohmann::json::array())) {
			Engine::PrefabInstanceData validated;
			if (!Engine::FromJson(instance, validated)) {
				return 39;
			}
		}
		std::cout << "Prefab recovery passed\n" << diagnostic;
		return 0;
	}

	if (1 < argc && std::string_view(argv[1]) == "--scene-single-load") {

		if (!RunTest("TestSingleSceneLoadReservation", TestSingleSceneLoadReservation)) {
			std::cerr << "Single scene load reservation test failed\n";
			return 38;
		}
		std::cout << "Single scene load reservation test passed\n";
		return 0;
	}

	if (1 < argc && std::string_view(argv[1]) == "--scene-copy") {

		if (!RunTest("TestSceneAssetStorage", TestSceneAssetStorage) || !RunTest("TestSceneAssetCopy", TestSceneAssetCopy) ||
			!RunTest("TestExternalActors", TestExternalActors) ||
			!RunTest("TestPrefabPropagationAndNestedInstances", TestPrefabPropagationAndNestedInstances)) {
			std::cerr << "Scene asset copy test failed\n";
			return 37;
		}
		std::cout << "Scene asset copy test passed\n";
		return 0;
	}

	if (1 < argc && std::string_view(argv[1]) == "--scene-lifecycle") {
		if (!RunTest("TestSceneLifecycleContext", TestSceneLifecycleContext)) {
			std::cerr << "Scene lifecycle context test failed\n";
			return 36;
		}
		std::cout << "Scene lifecycle context test passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--canvas-ui") {
		if (!RunTest("TestCanvasNavigationTable", TestCanvasNavigationTable)) {
			std::cerr << "Canvas UI test failed\n";
			return 35;
		}
		std::cout << "Canvas UI test passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--prefab-immediate") {
		return TestPrefabImmediateHierarchy() ? 0 : 33;
	}
	if (1 < argc && std::string_view(argv[1]) == "--prefab-nested") {
		return TestPrefabPropagationAndNestedInstances() ? 0 : 34;
	}
	if (1 < argc && std::string_view(argv[1]) == "--prefab") {
		if (!RunTest("TestPrefabImmediateHierarchy", TestPrefabImmediateHierarchy) ||
			!RunTest("TestPrefabPropagationAndNestedInstances", TestPrefabPropagationAndNestedInstances)) {
			std::cerr << "Prefab test failed\n";
			return 34;
		}
		std::cout << "Prefab test passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--physics") {
		if (!RunTest("TestBoxInternalFaces", TestBoxInternalFaces) ||
			!RunTest("TestRigidbodyBoxSeams", TestRigidbodyBoxSeams) ||
			!RunTest("TestBoxSeamNeighborState", TestBoxSeamNeighborState) ||
			!RunTest("TestBoxSeamLanding", TestBoxSeamLanding) ||
			!RunTest("TestRigidbody2DRestingContact", TestRigidbody2DRestingContact) ||
			!RunTest("TestInactivePhysicsSystems", TestInactivePhysicsSystems) ||
			!RunTest("TestCollisionParentCoordinates", TestCollisionParentCoordinates) ||
			!RunTest("TestCollisionWorldShapes", TestCollisionWorldShapes) ||
			!RunTest("TestCollisionSettingsPersistence", TestCollisionSettingsPersistence) ||
			!RunTest("TestEditCollisionState", TestEditCollisionState) ||
			!RunTest("TestCapsuleCollisions", TestCapsuleCollisions)) {
			std::cerr << "Physics collision test failed\n";
			return 27;
		}
		std::cout << "Physics collision test passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--paths") {
		if (!RunTest("TestUTF8Path", TestUTF8Path)) {
			std::cerr << "UTF-8 path failed\n";
			return 24;
		}
		std::cout << "UTF-8 path passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--texture-import") {
		if (!RunTest("TestTextureImportSettings", TestTextureImportSettings)) {
			std::cerr << "Texture import settings failed\n";
			return 25;
		}
		std::cout << "Texture import settings passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--screen-space-outline") {
		if (!RunTest("TestScreenSpaceOutlineSerialization", TestScreenSpaceOutlineSerialization) ||
			!RunTest("TestScreenSpaceOutlineBinding", TestScreenSpaceOutlineBinding)) {
			std::cerr << "Screen space outline binding failed\n";
			return 10;
		}
		std::cout << "Screen space outline binding passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--ecs") {
		if ((!RunTest("TestECSChunkStorage", TestECSChunkStorage) ||
				!RunTest("TestECSStructureSafety", TestECSStructureSafety)) ||
			!RunTest("TestECSExternalStorage", TestECSExternalStorage) || !RunTest("TestECSRuntimeData", TestECSRuntimeData) ||
			!RunTest("TestNonTrivialDynamicBuffer", TestNonTrivialDynamicBuffer) ||
			!RunTest("TestPrefabImmediateHierarchy", TestPrefabImmediateHierarchy) ||
			!RunTest("TestPrefabPropagationAndNestedInstances", TestPrefabPropagationAndNestedInstances) ||
			!RunTest("TestTransformDirtyHierarchy", TestTransformDirtyHierarchy) ||
			!RunTest("TestTransformDimensionSerialization", TestTransformDimensionSerialization) ||
			!RunTest("TestScreenSpaceOutlineSerialization", TestScreenSpaceOutlineSerialization) ||
			!RunTest("TestScreenSpaceOutlineBinding", TestScreenSpaceOutlineBinding) ||
			!RunTest("TestScriptExecutionOrderSettings", TestScriptExecutionOrderSettings) ||
			!RunTest("TestScriptProfiler", TestScriptProfiler) || !RunTest("TestScriptFieldStorage", TestScriptFieldStorage) ||
			!RunTest("TestCanvasNavigationTable", TestCanvasNavigationTable)) {
			std::cerr << "ECS chunk storage failed\n";
			return 10;
		}
		std::cout << "ECS chunk storage passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--shader-graph") {

		if (!RunTest("TestShaderGraphCompile", TestShaderGraphCompile) ||
			!RunTest("TestRenderFeatureRuntimeOverrides", TestRenderFeatureRuntimeOverrides) ||
			!RunTest("TestRayTracingPipelineSerialization", TestRayTracingPipelineSerialization)) {
			std::cerr << "Shader Graph compilation failed\n";
			return 18;
		}
		std::cout << "Shader Graph compilation passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--materials") {

		if (!RunTest("TestBlendStates", TestBlendStates) || !RunTest("TestMeshBatchInvalidation", TestMeshBatchInvalidation) ||
			!RunTest("TestMaterialParameters", TestMaterialParameters) ||
			!RunTest("TestMaterialParameterHash", TestMaterialParameterHash) ||
			!RunTest("TestMaterialReflectionCache", TestMaterialReflectionCache) ||
			!RunTest("TestMaterialResolverIndexChanges", TestMaterialResolverIndexChanges) ||
			!RunTest("TestMeshAuthoringCache", TestMeshAuthoringCache) ||
			!RunTest("TestPrimitiveTangents", TestPrimitiveTangents) ||
			!RunTest("TestShaderReflectionMerge", TestShaderReflectionMerge) ||
			!RunTest("TestRootSignaturePlanning", TestRootSignaturePlanning)) {
			std::cerr << "Material parameter storage failed\n";
			return 17;
		}
		std::cout << "Material parameter storage passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--render-features") {

		if (!RunTest("TestRenderTargetSizing", TestRenderTargetSizing) ||
			!RunTest("TestRenderFeatureRuntimeOverrides", TestRenderFeatureRuntimeOverrides) ||
			!RunTest("TestShaderPathDependencies", TestShaderPathDependencies) ||
			!RunTest("TestRenderFeatureProfile", TestRenderFeatureProfile) ||
			!RunTest("TestPostProcessSourceExtension", TestPostProcessSourceExtension) ||
			!RunTest("TestPostProcessPublication", TestPostProcessPublication)) {
			std::cerr << "Render Feature test failed\n";
			return 22;
		}
		std::cout << "Render Feature test passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--render-feature-profile") {

		if (!RunTest("TestRenderFeatureProfile", TestRenderFeatureProfile)) {
			std::cerr << "Render Feature profile test failed\n";
			return 28;
		}
		std::cout << "Render Feature profile test passed\n";
		return 0;
	}

	return std::nullopt;
}
