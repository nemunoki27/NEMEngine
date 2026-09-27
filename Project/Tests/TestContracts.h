#pragma once

namespace NEMTests {
	bool TestManagedLifecycleIntegration();

	bool TestProfilerContracts();

	bool TestAssetGUIDRoundTrip();
	bool TestAssetDatabaseTransactions();
	bool TestAssetWatcherLifetime();
	bool TestAssetDependencyCandidates();
	bool TestBuiltinShaderSources();
	bool TestMeshShaderConstantLayout();
	bool TestContentHash();
	bool TestPackageResolver();
	bool TestVirtualPath();
	bool TestCanonicalSceneData();
	bool TestJsonSemanticMerge();
	bool TestSubScenes();
	bool TestSceneSnapshotTransactions();
	bool TestCreationScopes();
	bool TestSingleSceneLoadReservation();
	bool TestSceneAssetStorage();
	bool TestExternalActors();
	bool TestSceneAssetCopy();
	bool TestECSChunkStorage();
	bool TestECSStructureSafety();
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
	bool TestGraphicsFeatureSelection();
	bool TestBlendStates();
	bool TestMeshBatchInvalidation();
	bool TestMaterialParameters();
	bool TestMeshAuthoringCache();
	bool TestPrimitiveTangents();
	bool TestTransformDimensionSerialization();
	bool TestScreenSpaceOutlineBinding();
	bool TestScreenSpaceOutlineSerialization();
	bool TestScriptProfiler();
	bool TestScriptFieldStorage();
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
	bool TestGPURetirement(bool hardware = false);
}
