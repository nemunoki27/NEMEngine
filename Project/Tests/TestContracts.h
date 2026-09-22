#pragma once

namespace NEMTests {

	bool TestProfilerContracts();

	bool TestAssetGUIDRoundTrip();
	bool TestBuiltinShaderSources();
	bool TestContentHash();
	bool TestPackageResolver();
	bool TestVirtualPath();
	bool TestCanonicalSceneData();
	bool TestJsonSemanticMerge();
	bool TestSubScenes();
	bool TestSingleSceneLoadReservation();
	bool TestSceneAssetStorage();
	bool TestExternalActors();
	bool TestSceneAssetCopy();
	bool TestECSChunkStorage();
	bool TestPrefabImmediateHierarchy();
	bool TestSceneLifecycleContext();
	bool TestPrefabPropagationAndNestedInstances();
	bool TestECSExternalStorage();
	bool TestECSRuntimeData();
	bool TestNonTrivialDynamicBuffer();
	bool TestSerializationClone();
	bool TestTransformDirtyHierarchy();
	bool TestBoxInternalFaces();
	bool TestRigidbodyBoxSeams();
	bool TestBoxSeamNeighborState();
	bool TestBoxSeamLanding();
	bool TestRigidbody2DRestingContact();
	bool TestInactivePhysicsSystems();
	bool TestEditCollisionState();
	bool TestCapsuleCollisions();
	bool TestMeshLODGeneration();
	bool TestBlendStates();
	bool TestMeshBatchInvalidation();
	bool TestMaterialParameters();
	bool TestTransformDimensionSerialization();
	bool TestScreenSpaceOutlineBinding();
	bool TestScreenSpaceOutlineSerialization();
	bool TestScriptProfiler();
	bool TestScriptExecutionOrderSettings();
	bool TestUTF8Path();
	bool TestTextureImportSettings();
	bool TestShaderReflectionMerge();
	bool TestRenderFeatureRuntimeOverrides();
	bool TestShaderPathDependencies();
	bool TestRayTracingPipelineSerialization();
	bool TestShaderGraphCompile();
	bool TestRenderFeatureProfile();
	bool TestPostProcessSourceExtension();
	bool TestCanvasNavigationTable();
}
