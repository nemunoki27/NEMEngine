#include "TestContracts.h"

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
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSemanticMerge.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/DimensionType.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Assets/Utility/AssetTypeResolver.h>
#include <Engine/Core/Rendering/Assets/RenderPipelineAsset.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Pipelines/Stage/BlendState.h>
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>
#include <Engine/Core/Rendering/Textures/TextureImportSettings.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshletBuilder.h>
#include <Engine/Core/Rendering/Pipelines/BuiltinShaderSource.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/DxObject/Core/DxShaderReflectionParser.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphSettingsImporter.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Shaders/ShaderCookStorage.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessAssetGenerator.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureRuntimeOverrides.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphCompiler.h>
#include <Engine/Core/Physics/Collision/CollisionRaycast.h>
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/Runtime/Packages/PackageResolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Scripting/Managed/ScriptExecutionOrderSettings.h>
#include <Engine/Core/Scripting/Managed/Generated/ManagedComponentBindings.generated.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptUtility.h>
#include <Engine/Core/Scripting/Managed/ManagedWorldRegistry.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ScriptProfiler.h>
#include <Engine/Core/World/Behavior/Registry/BehaviorTypeRegistry.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ScreenSpaceOutlineComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/Scripting/Managed/ManagedScriptTypes.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UISelectableComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Systems/Physics/CollisionSystem.h>
#include <Engine/Core/World/Systems/Physics/PhysicsSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>

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

using namespace NEMTests;

int main(int argc, char* argv[]) {

	if (1 < argc && std::string_view(argv[1]) == "--editor") {
		if (!TestEditorContracts()) return 43;
		std::cout << "Editor tests passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--gameplay") {
		if (!TestGameplayContracts()) return 42;
		std::cout << "Gameplay tests passed\n";
		return 0;
	}

	if (1 < argc && std::string_view(argv[1]) == "--foundation") {
		if (!TestFoundationContracts()) return 41;
		std::cout << "Foundation tests passed\n";
		return 0;
	}

	if (1 < argc && std::string_view(argv[1]) == "--scene-storage") {
		if (!TestSceneAssetStorage()) return 40;
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
			if (!Engine::FromJson(instance, validated)) return 39;
		}
		std::cout << "Prefab recovery passed\n" << diagnostic;
		return 0;
	}

	if (1 < argc && std::string_view(argv[1]) == "--scene-single-load") {

		if (!TestSingleSceneLoadReservation()) {
			std::cerr << "Single scene load reservation test failed\n";
			return 38;
		}
		std::cout << "Single scene load reservation test passed\n";
		return 0;
	}

	if (1 < argc && std::string_view(argv[1]) == "--scene-copy") {

		if (!TestSceneAssetStorage() || !TestSceneAssetCopy() || !TestExternalActors() || !TestPrefabPropagationAndNestedInstances()) {
			std::cerr << "Scene asset copy test failed\n";
			return 37;
		}
		std::cout << "Scene asset copy test passed\n";
		return 0;
	}

	if (1 < argc && std::string_view(argv[1]) == "--scene-lifecycle") {
		if (!TestSceneLifecycleContext()) {
			std::cerr << "Scene lifecycle context test failed\n";
			return 36;
		}
		std::cout << "Scene lifecycle context test passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--canvas-ui") {
		if (!TestCanvasNavigationTable()) {
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
		if (!TestPrefabImmediateHierarchy() || !TestPrefabPropagationAndNestedInstances()) {
			std::cerr << "Prefab test failed\n";
			return 34;
		}
		std::cout << "Prefab test passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--physics") {
		if (!TestBoxInternalFaces() || !TestRigidbodyBoxSeams() || !TestBoxSeamNeighborState() || !TestBoxSeamLanding() ||
			!TestRigidbody2DRestingContact() || !TestInactivePhysicsSystems() ||
			!TestEditCollisionState() ||
			!TestCapsuleCollisions()) {
			std::cerr << "Physics collision test failed\n";
			return 27;
		}
		std::cout << "Physics collision test passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--paths") {
		if (!TestUTF8Path()) {
			std::cerr << "UTF-8 path failed\n";
			return 24;
		}
		std::cout << "UTF-8 path passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--texture-import") {
		if (!TestTextureImportSettings()) {
			std::cerr << "Texture import settings failed\n";
			return 25;
		}
		std::cout << "Texture import settings passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--screen-space-outline") {
		if (!TestScreenSpaceOutlineSerialization() || !TestScreenSpaceOutlineBinding()) {
			std::cerr << "Screen space outline binding failed\n";
			return 10;
		}
		std::cout << "Screen space outline binding passed\n";
		return 0;
	}
	if (1 < argc && std::string_view(argv[1]) == "--ecs") {
		if (!TestECSChunkStorage() || !TestECSExternalStorage() ||
			!TestECSRuntimeData() || !TestNonTrivialDynamicBuffer() ||
			!TestPrefabImmediateHierarchy() ||
			!TestPrefabPropagationAndNestedInstances() ||
			!TestTransformDirtyHierarchy() ||
			!TestTransformDimensionSerialization() ||
			!TestScreenSpaceOutlineSerialization() || !TestScreenSpaceOutlineBinding() ||
			!TestScriptExecutionOrderSettings() || !TestScriptProfiler() ||
			!TestCanvasNavigationTable()) {
			std::cerr << "ECS chunk storage failed\n";
			return 10;
		}
		std::cout << "ECS chunk storage passed\n";
		return 0;
	}
	if (1 < argc &&
		std::string_view(argv[1]) == "--shader-graph") {

		if (!TestShaderGraphCompile() ||
			!TestRenderFeatureRuntimeOverrides() ||
			!TestRayTracingPipelineSerialization()) {
			std::cerr << "Shader Graph compilation failed\n";
			return 18;
		}
		std::cout << "Shader Graph compilation passed\n";
		return 0;
	}
	if (1 < argc &&
		std::string_view(argv[1]) == "--materials") {

		if (!TestBlendStates() ||
			!TestMeshBatchInvalidation() ||
			!TestMaterialParameters() ||
			!TestShaderReflectionMerge()) {
			std::cerr << "Material parameter storage failed\n";
			return 17;
		}
		std::cout << "Material parameter storage passed\n";
		return 0;
	}
	if (1 < argc &&
		std::string_view(argv[1]) == "--render-features") {

		if (!TestRenderFeatureRuntimeOverrides() ||
			!TestShaderPathDependencies() ||
			!TestRenderFeatureProfile() ||
			!TestPostProcessSourceExtension()) {
			std::cerr << "Render Feature test failed\n";
			return 22;
		}
		std::cout << "Render Feature test passed\n";
		return 0;
	}
	if (1 < argc &&
		std::string_view(argv[1]) == "--render-feature-profile") {

		if (!TestRenderFeatureProfile()) {
			std::cerr << "Render Feature profile test failed\n";
			return 28;
		}
		std::cout << "Render Feature profile test passed\n";
		return 0;
	}

	if (!TestEditorContracts()) {
		return 43;
	}
	if (!TestWindowFileDropConversion()) {
		return 42;
	}
	if (!TestGameplayContracts()) {
		return 42;
	}
	if (!TestFoundationContracts()) {
		return 41;
	}
	if (!TestAssetGUIDRoundTrip()) {
		std::cerr << "AssetGUID round-trip failed\n";
		return 1;
	}
	if (!TestContentHash()) {
		std::cerr << "Content hash failed\n";
		return 2;
	}
	if (!TestPackageResolver()) {
		std::cerr << "Package resolver failed\n";
		return 3;
	}
	if (!TestVirtualPath()) {
		std::cerr << "Virtual path failed\n";
		return 4;
	}
	if (!TestUTF8Path()) {
		std::cerr << "UTF-8 path failed\n";
		return 24;
	}
	if (!TestTextureImportSettings()) {
		std::cerr << "Texture import settings failed\n";
		return 25;
	}
	if (!TestCanonicalSceneData()) {
		std::cerr << "Canonical scene data failed\n";
		return 5;
	}
	if (!TestJsonSemanticMerge()) {
		std::cerr << "Semantic JSON merge failed\n";
		return 6;
	}
	if (!TestSubScenes()) {
		std::cerr << "SubScene failed\n";
		return 7;
	}
	if (!TestSingleSceneLoadReservation()) {
		std::cerr << "Single scene load reservation failed\n";
		return 38;
	}
	if (!TestSceneLifecycleContext()) {
		std::cerr << "Scene lifecycle context failed\n";
		return 36;
	}
	if (!TestSceneAssetStorage() || !TestExternalActors() || !TestSceneAssetCopy()) {
		std::cerr << "ExternalActors failed\n";
		return 8;
	}
	if (!TestBuiltinShaderSources()) {
		std::cerr << "Builtin shader source resolution failed\n";
		return 9;
	}
	if (!TestECSChunkStorage()) {
		std::cerr << "ECS chunk storage failed\n";
		return 10;
	}
	if (!TestPrefabImmediateHierarchy()) {
		std::cerr << "Prefab immediate hierarchy failed\n";
		return 33;
	}
	if (!TestPrefabPropagationAndNestedInstances()) {
		std::cerr << "Prefab propagation and nested instances failed\n";
		return 34;
	}
	if (!TestECSExternalStorage()) {
		std::cerr << "ECS external storage failed\n";
		return 11;
	}
	if (!TestECSRuntimeData()) {
		std::cerr << "ECS runtime data failed\n";
		return 12;
	}
	if (!TestNonTrivialDynamicBuffer()) {
		std::cerr << "ECS non-trivial buffer failed\n";
		return 13;
	}
	if (!TestTransformDirtyHierarchy()) {
		std::cerr << "Transform dirty hierarchy failed\n";
		return 14;
	}
	if (!TestTransformDimensionSerialization()) {
		std::cerr << "Transform dimension serialization failed\n";
		return 26;
	}
	if (!TestScreenSpaceOutlineSerialization() || !TestScreenSpaceOutlineBinding()) {
		std::cerr << "Screen space outline serialization failed\n";
		return 29;
	}
	if (!TestScriptExecutionOrderSettings() || !TestScriptProfiler()) {
		std::cerr << "Script execution order settings failed\n";
		return 35;
	}
	if (!TestBoxInternalFaces() || !TestRigidbodyBoxSeams() || !TestBoxSeamNeighborState() || !TestBoxSeamLanding()) {
		std::cerr << "Box collider seam contact failed\n";
		return 27;
	}
	if (!TestRigidbody2DRestingContact()) {
		std::cerr << "Rigidbody2D resting contact failed\n";
		return 27;
	}
	if (!TestInactivePhysicsSystems()) {
		std::cerr << "Inactive physics systems failed\n";
		return 32;
	}
	if (!TestEditCollisionState()) {
		std::cerr << "Edit collision state failed\n";
		return 31;
	}
	if (!TestCapsuleCollisions()) {
		std::cerr << "Capsule collision failed\n";
		return 28;
	}
	if (!TestSerializationClone()) {
		std::cerr << "Serialization clone failed\n";
		return 15;
	}
	if (!TestMeshLODGeneration()) {
		std::cerr << "Mesh LOD generation failed\n";
		return 16;
	}
	if (!TestBlendStates()) {
		std::cerr << "Blend state failed\n";
		return 30;
	}
	if (!TestMeshBatchInvalidation() || !TestMaterialParameters()) {
		std::cerr << "Material parameter storage failed\n";
		return 17;
	}
	if (!TestShaderReflectionMerge()) {
		std::cerr << "Shader reflection merge failed\n";
		return 23;
	}
	if (!TestRenderFeatureRuntimeOverrides()) {
		std::cerr << "Render Feature runtime overrides failed\n";
		return 20;
	}
	if (!TestShaderPathDependencies()) {
		std::cerr << "Shader path dependencies failed\n";
		return 32;
	}
	if (!TestRayTracingPipelineSerialization()) {
		std::cerr << "Ray Tracing pipeline serialization failed\n";
		return 21;
	}
	if (!TestShaderGraphCompile()) {
		std::cerr << "Shader Graph compilation failed\n";
		return 18;
	}
	if (!TestRenderFeatureProfile() ||
		!TestPostProcessSourceExtension()) {
		std::cerr << "RenderFeature profile failed\n";
		return 22;
	}
	if (!TestProfilerContracts()) {
		std::cerr << "Profiler contracts failed\n";
		return 44;
	}
	std::cout << "NEMTests passed\n";
	return 0;
}
