#include "TestSuite.h"

//============================================================================
//	include
//============================================================================
#include "TestRunner.h"
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

// c++
#include <iostream>

using namespace NEMTests;

// 標準の検証を既定の順序で実行する
int NEMTests::RunAllTests() {

	if (!RunTest("TestEditorContracts", TestEditorContracts)) {
		return 43;
	}
	if (!RunTest("TestWindowFileDropConversion", TestWindowFileDropConversion)) {
		return 42;
	}
	if (!RunTest("TestGameplayContracts", TestGameplayContracts)) {
		return 42;
	}
	if (!RunTest("TestFoundationContracts", TestFoundationContracts)) {
		return 41;
	}
	if (!RunTest("TestAssetGUIDRoundTrip", TestAssetGUIDRoundTrip) ||
		!RunTest("TestAssetDatabaseTransactions", TestAssetDatabaseTransactions) ||
		!RunTest("TestFontGenerationContracts", TestFontGenerationContracts) ||
		!RunTest("TestAssetWatcherLifetime", TestAssetWatcherLifetime) ||
		!RunTest("TestAssetDependencyCandidates", TestAssetDependencyCandidates)) {
		std::cerr << "AssetGUID round-trip failed\n";
		return 1;
	}
	if (!RunTest("TestContentHash", TestContentHash)) {
		std::cerr << "Content hash failed\n";
		return 2;
	}
	if (!RunTest("TestPackageResolver", TestPackageResolver)) {
		std::cerr << "Package resolver failed\n";
		return 3;
	}
	if (!RunTest("TestVirtualPath", TestVirtualPath)) {
		std::cerr << "Virtual path failed\n";
		return 4;
	}
	if (!RunTest("TestUTF8Path", TestUTF8Path)) {
		std::cerr << "UTF-8 path failed\n";
		return 24;
	}
	if (!RunTest("TestEditorAssetWorkflow", TestEditorAssetWorkflow)) { return 53; }
	if (!RunTest("TestProjectGitIgnoreDocument", TestProjectGitIgnoreDocument)) { return 54; }
	if (!RunTest("TestTextureImportSettings", TestTextureImportSettings)) {
		std::cerr << "Texture import settings failed\n";
		return 25;
	}
	if (!RunTest("TestCanonicalSceneData", TestCanonicalSceneData)) {
		std::cerr << "Canonical scene data failed\n";
		return 5;
	}
	if (!RunTest("TestJsonSemanticMerge", TestJsonSemanticMerge)) {
		std::cerr << "Semantic JSON merge failed\n";
		return 6;
	}
	if (!RunTest("TestSubScenes", TestSubScenes) || !RunTest("TestSceneSnapshotTransactions", TestSceneSnapshotTransactions)) {
		std::cerr << "SubScene failed\n";
		return 7;
	}
	if (!RunTest("TestSingleSceneLoadReservation", TestSingleSceneLoadReservation)) {
		std::cerr << "Single scene load reservation failed\n";
		return 38;
	}
	if (!RunTest("TestSceneLifecycleContext", TestSceneLifecycleContext)) {
		std::cerr << "Scene lifecycle context failed\n";
		return 36;
	}
	if (!RunTest("TestSceneAssetStorage", TestSceneAssetStorage) || !RunTest("TestExternalActors", TestExternalActors) ||
		!RunTest("TestSceneAssetCopy", TestSceneAssetCopy)) {
		std::cerr << "ExternalActors failed\n";
		return 8;
	}
	if (!RunTest("TestBuiltinShaderSources", TestBuiltinShaderSources)) {
		std::cerr << "Builtin shader source resolution failed\n";
		return 9;
	}
	if (!RunTest("TestLineShapeSegments", TestLineShapeSegments)) {
		return 44;
	}
	if (!RunTest("TestMeshShaderConstantLayout", TestMeshShaderConstantLayout)) {
		std::cerr << "Mesh shader constant layout failed\n";
		return 9;
	}
	if ((!RunTest("TestECSChunkStorage", TestECSChunkStorage) || !RunTest("TestECSStructureSafety", TestECSStructureSafety))) {
		std::cerr << "ECS chunk storage failed\n";
		return 10;
	}
	if (!RunTest("TestPrefabImmediateHierarchy", TestPrefabImmediateHierarchy)) {
		std::cerr << "Prefab immediate hierarchy failed\n";
		return 33;
	}
	if (!RunTest("TestPrefabPropagationAndNestedInstances", TestPrefabPropagationAndNestedInstances)) {
		std::cerr << "Prefab propagation and nested instances failed\n";
		return 34;
	}
	if (!RunTest("TestECSExternalStorage", TestECSExternalStorage)) {
		std::cerr << "ECS external storage failed\n";
		return 11;
	}
	if (!RunTest("TestECSRuntimeData", TestECSRuntimeData)) {
		std::cerr << "ECS runtime data failed\n";
		return 12;
	}
	if (!RunTest("TestNonTrivialDynamicBuffer", TestNonTrivialDynamicBuffer)) {
		std::cerr << "ECS non-trivial buffer failed\n";
		return 13;
	}
	if (!RunTest("TestTransformDirtyHierarchy", TestTransformDirtyHierarchy)) {
		std::cerr << "Transform dirty hierarchy failed\n";
		return 14;
	}
	if (!RunTest("TestTransformDimensionSerialization", TestTransformDimensionSerialization)) {
		std::cerr << "Transform dimension serialization failed\n";
		return 26;
	}
	if (!RunTest("TestScreenSpaceOutlineSerialization", TestScreenSpaceOutlineSerialization) ||
		!RunTest("TestScreenSpaceOutlineBinding", TestScreenSpaceOutlineBinding)) {
		std::cerr << "Screen space outline serialization failed\n";
		return 29;
	}
	if (!RunTest("TestManagedBuildDiagnostics", TestManagedBuildDiagnostics) ||
		!RunTest("TestScriptExecutionOrderSettings", TestScriptExecutionOrderSettings) ||
		!RunTest("TestScriptFieldStorage", TestScriptFieldStorage) || !RunTest("TestManagedSchemaCache", TestManagedSchemaCache) ||
		!RunTest("TestScriptProfiler", TestScriptProfiler)) {
		std::cerr << "Script execution order settings failed\n";
		return 35;
	}
	if (!RunTest("TestBoxInternalFaces", TestBoxInternalFaces) || !RunTest("TestRigidbodyBoxSeams", TestRigidbodyBoxSeams) ||
		!RunTest("TestBoxSeamNeighborState", TestBoxSeamNeighborState) || !RunTest("TestBoxSeamLanding", TestBoxSeamLanding)) {
		std::cerr << "Box collider seam contact failed\n";
		return 27;
	}
	if (!RunTest("TestRigidbody2DRestingContact", TestRigidbody2DRestingContact)) {
		std::cerr << "Rigidbody2D resting contact failed\n";
		return 27;
	}
	if (!RunTest("TestInactivePhysicsSystems", TestInactivePhysicsSystems) ||
		!RunTest("TestCollisionParentCoordinates", TestCollisionParentCoordinates) ||
		!RunTest("TestCollisionWorldShapes", TestCollisionWorldShapes) ||
		!RunTest("TestCollisionSettingsPersistence", TestCollisionSettingsPersistence)) {
		std::cerr << "Inactive physics systems failed\n";
		return 32;
	}
	if (!RunTest("TestEditCollisionState", TestEditCollisionState)) {
		std::cerr << "Edit collision state failed\n";
		return 31;
	}
	if (!RunTest("TestCapsuleCollisions", TestCapsuleCollisions) ||
		!RunTest("TestPhysicsQueryTriggers", TestPhysicsQueryTriggers)) {
		std::cerr << "Capsule collision failed\n";
		return 28;
	}
	if (!RunTest("TestSerializationClone", TestSerializationClone)) {
		std::cerr << "Serialization clone failed\n";
		return 15;
	}
	if (!RunTest("TestMeshLODGeneration", TestMeshLODGeneration)) {
		std::cerr << "Mesh LOD generation failed\n";
		return 16;
	}
	if (!RunTest("TestGraphicsFeatureSelection", TestGraphicsFeatureSelection)) {
		std::cerr << "Graphics feature selection failed\n";
		return 16;
	}
	if (!RunTest("TestBlendStates", TestBlendStates)) {
		std::cerr << "Blend state failed\n";
		return 30;
	}
	if (!RunTest("TestMeshBatchInvalidation", TestMeshBatchInvalidation) ||
		!RunTest("TestMaterialParameters", TestMaterialParameters) ||
		!RunTest("TestMaterialParameterHash", TestMaterialParameterHash) ||
		!RunTest("TestMeshAuthoringCache", TestMeshAuthoringCache) ||
		!RunTest("TestPrimitiveTangents", TestPrimitiveTangents)) {
		std::cerr << "Material parameter storage failed\n";
		return 17;
	}
	if (!RunTest("TestShaderReflectionMerge", TestShaderReflectionMerge) ||
		!RunTest("TestRootSignaturePlanning", TestRootSignaturePlanning)) {
		std::cerr << "Shader reflection merge failed\n";
		return 23;
	}
	if (!RunTest("TestRenderFeatureRuntimeOverrides", TestRenderFeatureRuntimeOverrides)) {
		std::cerr << "Render Feature runtime overrides failed\n";
		return 20;
	}
	if (!RunTest("TestShaderPathDependencies", TestShaderPathDependencies)) {
		std::cerr << "Shader path dependencies failed\n";
		return 32;
	}
	if (!RunTest("TestRayTracingPipelineSerialization", TestRayTracingPipelineSerialization)) {
		std::cerr << "Ray Tracing pipeline serialization failed\n";
		return 21;
	}
	if (!RunTest("TestShaderGraphCompile", TestShaderGraphCompile)) {
		std::cerr << "Shader Graph compilation failed\n";
		return 18;
	}
	if (!RunTest("TestRendererLayerCulling", TestRendererLayerCulling) ||
		!RunTest("TestRenderCameraHistory", TestRenderCameraHistory) ||
		!RunTest("TestSceneGridProjection", TestSceneGridProjection) ||
		!RunTest("TestRenderTargetSizing", TestRenderTargetSizing) ||
		!RunTest("TestProjectAssetCopyTransaction", TestProjectAssetCopyTransaction) ||
		!RunTest("TestModelImportBundle", TestModelImportBundle) ||
		!RunTest("TestFBXImport", TestFBXImport) ||
		!RunTest("TestProjectAssetMoveTransaction", TestProjectAssetMoveTransaction) ||
		!RunTest("TestMaterialCreationFailures", TestMaterialCreationFailures) ||
		!RunTest("TestMaterialReflectionCache", TestMaterialReflectionCache) ||
		!RunTest("TestMaterialResolverIndexChanges", TestMaterialResolverIndexChanges) ||
		!RunTest("TestAssetDocumentRecovery", TestAssetDocumentRecovery) ||
		!RunTest("TestShaderGraphPublication", TestShaderGraphPublication) ||
		!RunTest("TestSceneViewCameraSettings", TestSceneViewCameraSettings) ||
		!RunTest("TestRenderFeatureProfile", TestRenderFeatureProfile) ||
		!RunTest("TestRenderPassesSelectionRequests", TestRenderPassesSelectionRequests) ||
		!RunTest("TestPerformanceGridCaptureRetry", TestPerformanceGridCaptureRetry) ||
		!RunTest("TestPerformanceGridUpdateRollback", TestPerformanceGridUpdateRollback) ||
		!RunTest("TestPerformanceGridWorldEnd", TestPerformanceGridWorldEnd) ||
		!RunTest("TestEditorTransactionRollback", TestEditorTransactionRollback) ||
		!RunTest("TestRenderPassesReflectionCache", TestRenderPassesReflectionCache) ||
		!RunTest("TestPostProcessSourceExtension", TestPostProcessSourceExtension) ||
		!RunTest("TestPostProcessPublication", TestPostProcessPublication)) {
		std::cerr << "RenderFeature profile failed\n";
		return 22;
	}
	if (!RunTest("TestProfilerContracts", TestProfilerContracts)) {
		std::cerr << "Profiler contracts failed\n";
		return 44;
	}
	std::cout << "NEMTests passed\n";
	return 0;
}
