#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Primitive/ClothGPUSolver.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/World/Components/Physics/ClothComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Physics/ClothSystem.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingSceneGeometryUtility.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>

// c++
#include <cmath>
#include <limits>
#include <unordered_map>

namespace {

	bool TestNormalizedGeometryKeys() {

		using namespace Engine;
		const auto sameGeometry = [](const PrimitiveRendererComponent& first, const PrimitiveRendererComponent& second) {
			if (PrimitiveMeshGenerator::ComputeHash(first) != PrimitiveMeshGenerator::ComputeHash(second)) return false;
			PrimitiveMeshData a, b;
			PrimitiveMeshGenerator::Generate(first, a);
			PrimitiveMeshGenerator::Generate(second, b);
			if (a.vertices.size() != b.vertices.size() || a.indices != b.indices) return false;
			for (size_t index = 0; index < a.vertices.size(); ++index) {
				const auto& left = a.vertices[index];
				const auto& right = b.vertices[index];
				if (left.position != right.position || left.normal != right.normal || left.texcoord != right.texcoord ||
					left.tangent != right.tangent || left.tangentSign != right.tangentSign) return false;
			}
			return true;
		};
		// 最小分割数へ丸めた結果と元入力を同じ形状として共有する
		PrimitiveRendererComponent low, normalized;
		low.plane.divideX = -1;
		low.plane.divideY = 0;
		low.crossPlane.planeCount = 0;
		normalized.crossPlane.planeCount = 1;
		low.ring.divide = 0;
		normalized.ring.divide = 3;
		low.cylinder.radialDivide = 0;
		low.cylinder.heightDivide = 0;
		normalized.cylinder.radialDivide = 3;
		normalized.cylinder.heightDivide = 2;
		low.sphere.longitudeDivide = 0;
		low.sphere.latitudeDivide = 0;
		normalized.sphere.longitudeDivide = 3;
		normalized.sphere.latitudeDivide = 2;
		low.hemisphere.longitudeDivide = 0;
		low.hemisphere.latitudeDivide = 0;
		normalized.hemisphere.longitudeDivide = 3;
		normalized.hemisphere.latitudeDivide = 1;
		for (auto type : { PrimitiveType::Plane, PrimitiveType::CrossPlane, PrimitiveType::Ring,
			PrimitiveType::Cylinder, PrimitiveType::Sphere, PrimitiveType::Hemisphere, PrimitiveType::Cube }) {
			low.type = normalized.type = type;
			if (!sameGeometry(low, normalized)) return false;
		}
		// 円柱の偶数補正と重みの範囲補正もcache識別へ揃える
		low.type = normalized.type = PrimitiveType::Cylinder;
		low.cylinder.heightDivide = 3;
		normalized.cylinder.heightDivide = 4;
		low.cylinder.topRadiusWeight = 2.0f;
		normalized.cylinder.topRadiusWeight = 1.0f;
		low.cylinder.bottomRadiusWeight = -1.0f;
		low.cylinder.centerRadius = normalized.cylinder.centerRadius = 0.5f;
		if (!sameGeometry(low, normalized)) return false;
		low.type = normalized.type = PrimitiveType::Plane;
		low.plane.divideX = kMaxPrimitiveDivide + 7;
		normalized.plane.divideX = kMaxPrimitiveDivide;
		low.plane.pivot.x = -0.0f;
		normalized.plane.pivot.x = 0.0f;
		return sameGeometry(low, normalized);
	}

	bool TestFiniteParameters() {

		using namespace Engine;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float infinity = std::numeric_limits<float>::infinity();
		const auto check = [](PrimitiveRendererComponent invalid,
			PrimitiveRendererComponent expected) {

			PrimitiveMeshData mesh;
			PrimitiveMeshGenerator::Generate(invalid, mesh);
			if (PrimitiveMeshGenerator::ComputeHash(invalid) !=
				PrimitiveMeshGenerator::ComputeHash(expected)) {
				return false;
			}
			for (const PrimitiveMeshVertex& vertex : mesh.vertices) {
				if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
					!std::isfinite(vertex.position.z) || !std::isfinite(vertex.normal.x) ||
					!std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z) ||
					!std::isfinite(vertex.texcoord.x) || !std::isfinite(vertex.texcoord.y) ||
					!std::isfinite(vertex.tangent.x) || !std::isfinite(vertex.tangent.y) ||
					!std::isfinite(vertex.tangent.z)) {
					return false;
				}
			}
			return true;
		};

		PrimitiveRendererComponent expected;
		PrimitiveRendererComponent invalid = expected;
		invalid.type = expected.type = PrimitiveType::Plane;
		invalid.plane.size.x = nan;
		invalid.plane.pivot.y = infinity;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Ring;
		invalid.ring.outerRadius = nan;
		invalid.ring.endAngle = infinity;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Cylinder;
		invalid.cylinder.height = nan;
		invalid.cylinder.maxAngle = infinity;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Sphere;
		invalid.sphere.radius = nan;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Hemisphere;
		invalid.hemisphere.radius = infinity;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Cube;
		invalid.cube.size.z = nan;
		invalid.cube.pivot.x = infinity;
		return check(invalid, expected);
	}

	bool TestGeometryKeyCollision() {

		using namespace Engine;
		struct ZeroHash {

			size_t operator()(const PrimitiveGeometryKey&) const { return 0; }
		};

		PrimitiveRendererComponent plane;
		PrimitiveRendererComponent sphere;
		sphere.type = PrimitiveType::Sphere;
		const PrimitiveGeometryKey planeKey = PrimitiveMeshGenerator::ComputeKey(plane);
		const PrimitiveGeometryKey sphereKey = PrimitiveMeshGenerator::ComputeKey(sphere);
		if (planeKey == sphereKey) return false;

		// 同じhash bucketでも形状キーの比較で別要素に分ける
		std::unordered_map<PrimitiveGeometryKey, int, ZeroHash> cache;
		cache.emplace(planeKey, 1);
		cache.emplace(sphereKey, 2);
		return cache.size() == 2 && cache.at(planeKey) == 1 && cache.at(sphereKey) == 2;
	}

	bool ValidateGeometryCapacity(const Engine::PrimitiveMeshData& mesh) {

		if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0 ||
			mesh.vertices.size() > std::numeric_limits<uint32_t>::max() ||
			mesh.indices.size() > std::numeric_limits<uint32_t>::max()) {
			return false;
		}
		for (uint32_t index : mesh.indices) {
			if (index >= mesh.vertices.size()) return false;
		}
		return true;
	}

	bool TestGeometryCapacity() {

		using namespace Engine;
		PrimitiveRendererComponent renderer;
		PrimitiveMeshData mesh;
		for (PrimitiveType type : { PrimitiveType::Plane, PrimitiveType::CrossPlane,
			PrimitiveType::Ring, PrimitiveType::Cylinder, PrimitiveType::Sphere,
			PrimitiveType::Hemisphere, PrimitiveType::Cube }) {

			renderer.type = type;
			renderer.plane.divideX = renderer.plane.divideY = kMaxPrimitiveDivide;
			renderer.crossPlane.planeCount = kMaxPrimitiveDivide;
			renderer.ring.divide = kMaxPrimitiveDivide;
			renderer.cylinder.radialDivide = renderer.cylinder.heightDivide = kMaxPrimitiveDivide;
			renderer.sphere.longitudeDivide = renderer.sphere.latitudeDivide = kMaxPrimitiveDivide;
			renderer.hemisphere.longitudeDivide = renderer.hemisphere.latitudeDivide = kMaxPrimitiveDivide;
			PrimitiveMeshGenerator::Generate(renderer, mesh);
			if (!ValidateGeometryCapacity(mesh)) return false;
		}
		return true;
	}

	bool TestRaytracingCullFlags() {

		using namespace Engine::RaytracingSceneGeometryUtility;
		struct TestCase {

			D3D12_CULL_MODE cullMode;
			BOOL frontCounterClockwise;
			bool reverseWinding;
			D3D12_RAYTRACING_INSTANCE_FLAGS expected;
		};
		const TestCase cases[] = {
			{D3D12_CULL_MODE_NONE, FALSE, false, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE},
			{D3D12_CULL_MODE_NONE, TRUE, false, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE |
				D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE},
			{D3D12_CULL_MODE_BACK, FALSE, false, D3D12_RAYTRACING_INSTANCE_FLAG_NONE},
			{D3D12_CULL_MODE_BACK, TRUE, false, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE},
			{D3D12_CULL_MODE_FRONT, FALSE, false, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE},
			{D3D12_CULL_MODE_FRONT, TRUE, false, D3D12_RAYTRACING_INSTANCE_FLAG_NONE},
			{D3D12_CULL_MODE_NONE, FALSE, true, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE |
				D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE},
			{D3D12_CULL_MODE_NONE, TRUE, true, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE},
			{D3D12_CULL_MODE_BACK, FALSE, true, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE},
			{D3D12_CULL_MODE_BACK, TRUE, true, D3D12_RAYTRACING_INSTANCE_FLAG_NONE},
			{D3D12_CULL_MODE_FRONT, FALSE, true, D3D12_RAYTRACING_INSTANCE_FLAG_NONE},
			{D3D12_CULL_MODE_FRONT, TRUE, true, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE}
		};
		for (const auto& test : cases) {

			D3D12_RASTERIZER_DESC rasterizer{};
			rasterizer.CullMode = test.cullMode;
			rasterizer.FrontCounterClockwise = test.frontCounterClockwise;
			if (ToRaytracingCullFlags(rasterizer, test.reverseWinding) != test.expected) return false;
			if (!test.reverseWinding) {
				const uint32_t expectedFace = test.cullMode == D3D12_CULL_MODE_NONE ? 0u :
					(test.expected == D3D12_RAYTRACING_INSTANCE_FLAG_NONE ? 2u : 4u);
				Engine::RendererRenderSettings settings{};
				if (ResolveEmitterFaceFlags(rasterizer, &settings) != expectedFace) return false;
				settings.inheritMaterial = false;
				settings.cullMode = static_cast<Engine::RendererCullMode>(test.cullMode);
				rasterizer.CullMode = D3D12_CULL_MODE_NONE;
				if (ResolveEmitterFaceFlags(rasterizer, &settings) != expectedFace) return false;
			}
		}
		return true;
	}

	bool TestImportedMeshWinding() {

		using namespace Engine;
		const auto path = RuntimePaths::GetEngineProjectRoot() / "Tests/Fixtures/Decals/GameAssets/Models/Common/cube.obj";
		const auto mesh = MeshFileImporter::ImportFile({}, path, false);
		if (mesh.vertices.empty() || mesh.indices.empty()) return false;
		bool foundFace = false;
		for (size_t index = 0; index < mesh.indices.size(); index += 3) {

			const auto& a = mesh.vertices[mesh.indices[index + 0]];
			const auto& b = mesh.vertices[mesh.indices[index + 1]];
			const auto& c = mesh.vertices[mesh.indices[index + 2]];
			const Vector3 edge0(b.position.x - a.position.x, b.position.y - a.position.y, b.position.z - a.position.z);
			const Vector3 edge1(c.position.x - a.position.x, c.position.y - a.position.y, c.position.z - a.position.z);
			const auto face = Vector3::Cross(edge0, edge1);
			if (face.Length() <= 1e-6f) continue;
			if (Vector3::Dot(face, a.normal + b.normal + c.normal) <= 0.0f) return false;
			foundFace = true;
		}
		return foundFace;
	}

	bool CheckSimulatedClothGeometry(Engine::GraphicsCore& graphics, Engine::PrimitiveGeometryManager& manager, bool dxr) {

		using namespace Engine;
		AssetDatabase database;
		database.Init();
		if (!database.RebuildMeta({RuntimePaths::GetEngineAssetsRoot() / "Rendering/Builtin/Cloth"})) return false;
		RenderAssetLibrary assets;
		assets.Init(&database);
		PipelineStateCache pipelines;
		ECSWorld world;
		ClothSystem system;
		SystemContext context;
		context.mode = WorldMode::Play;
		context.deltaTime = 1.0f / 60.0f;
		auto& platform = graphics.GetDXObject();
		context.graphicsPlatform = &platform;
		context.renderAssetLibrary = &assets;
		context.assetDatabase = &database;
		const std::array owners{world.CreateEntity(), world.CreateEntity()};
		for (size_t i = 0; i < owners.size(); ++i) {
			const auto owner = owners[i];
			world.AddComponent<TransformComponent>(owner);
			auto& plane = world.AddComponent<PrimitiveRendererComponent>(owner);
			plane.plane.divideX = 12;
			plane.plane.divideY = 8;
			world.AddComponent<ClothComponent>(owner);
			ClothSettings settings;
			settings.gravityScale = settings.gustStrength = 0.0f;
			settings.windStrength = 8.0f;
			settings.windDirection = {0, 0, i == 0 ? 1.0f : -1.0f};
			SetClothSettings(world, owner, settings);
		}
		ComPtr<ID3D12Resource> readback;
		constexpr size_t bytes = 4 * sizeof(MeshVertex);
		DxUtils::CreateReadbackBufferResource(platform.GetDevice(), readback, bytes);
		const auto wait = ScopedCleanup([&]() noexcept { platform.WaitForGPU(); });
		auto& command = *platform.GetDxCommand();
		std::array<ID3D12Resource*, 2> blas{};
		for (int frame = 0; frame < 3; ++frame) {
			system.Update(world, context);
			graphics.BeginRenderFrame();
			graphics.TickFrameServices();
			manager.BeginFrame();
			command.SetDescriptorHeaps({graphics.GetSRVDescriptor().GetDescriptorHeap()});
			ClothSystem::DispatchGPU(world, graphics, assets, pipelines);
			const auto revision = TryGetCloth(world, owners.front())->runtime.revision;
			ClothSystem::DispatchGPU(world, graphics, assets, pipelines);
			bool valid = revision == TryGetCloth(world, owners.front())->runtime.revision;
			for (size_t i = 0; i < owners.size(); ++i) {
				const auto& r = TryGetCloth(world, owners[i])->runtime;
				const auto& plane = world.GetComponent<PrimitiveRendererComponent>(owners[i]);
				auto* g = manager.Resolve(graphics, plane, &world, owners[i]);
				if (!r.gpu || !g || g->GetVertexResource() != r.gpu->GetVertexResource() ||
					g->GetVertexDescriptorIndex() != r.gpu->GetVertexDescriptorIndex()) { valid = false; break; }
				if (dxr) {
					if (g->blasBuilt || !manager.EnsureBLAS(platform.GetDevice(), command.GetCommandList(), *g)) { valid = false; break; }
					valid &= !blas[i] || blas[i] == g->blas.GetResource();
					blas[i] = g->blas.GetResource();
					valid &= manager.Resolve(graphics, plane, &world, owners[i])->blasBuilt;
				}
				if (frame == 2) {
					const auto readable = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
					auto* vertexBuffer = g->GetVertexResource();
					command.TransitionBarriers(vertexBuffer, readable, D3D12_RESOURCE_STATE_COPY_SOURCE);
					command.GetCommandList()->CopyBufferRegion(readback.Get(), 2 * i * sizeof(MeshVertex), vertexBuffer, 0, sizeof(MeshVertex));
					command.GetCommandList()->CopyBufferRegion(readback.Get(), (2 * i + 1) * sizeof(MeshVertex), vertexBuffer,
						(g->vertexCount - 1) * sizeof(MeshVertex), sizeof(MeshVertex));
					command.TransitionBarriers(vertexBuffer, D3D12_RESOURCE_STATE_COPY_SOURCE, readable);
				}
			}
			graphics.SubmitRenderFrame();
			graphics.EndRenderFrame();
			if (!valid) return false;
		}
		platform.WaitForGPU();
		void* mapped = nullptr;
		const D3D12_RANGE range{0, bytes};
		if (FAILED(readback->Map(0, &range, &mapped))) return false;
		const auto* vertices = static_cast<const MeshVertex*>(mapped);
		const bool moved = std::abs(vertices[0].position.z) < 1e-4f && std::abs(vertices[2].position.z) < 1e-4f &&
			1e-4f < vertices[1].position.z && vertices[3].position.z < -1e-4f;
		const D3D12_RANGE empty{};
		readback->Unmap(0, &empty);
		if (!moved) return false;
		const std::weak_ptr<ClothGPUSolver> oldSolver = TryGetCloth(world, owners[0])->runtime.gpu;
		auto& resized = world.GetComponent<PrimitiveRendererComponent>(owners[0]);
		resized.plane.divideX = 24;
		system.Update(world, context);
		world.DestroyEntity(owners[1]);
		graphics.BeginRenderFrame();
		graphics.TickFrameServices();
		manager.BeginFrame();
		command.SetDescriptorHeaps({graphics.GetSRVDescriptor().GetDescriptorHeap()});
		ClothSystem::DispatchGPU(world, graphics, assets, pipelines);
		const auto* geometry = manager.Resolve(graphics, world.GetComponent<PrimitiveRendererComponent>(owners[0]), &world, owners[0]);
		const auto* record = TryGetCloth(world, owners[0]);
		const bool rebuilt = oldSolver.expired() && record->runtime.gpu && geometry && geometry->vertexCount == 25 * 9 &&
			geometry->GetVertexResource() == record->runtime.gpu->GetVertexResource();
		graphics.SubmitRenderFrame();
		graphics.EndRenderFrame();
		return rebuilt && SUCCEEDED(platform.GetDevice()->GetDeviceRemovedReason());
	}

	bool TestTriangleWinding() {

		using namespace Engine;
		PrimitiveRendererComponent renderer;
		PrimitiveMeshData mesh;
		for (PrimitiveType type : { PrimitiveType::Plane, PrimitiveType::CrossPlane,
			PrimitiveType::Ring, PrimitiveType::Cylinder, PrimitiveType::Sphere,
			PrimitiveType::Hemisphere, PrimitiveType::Cube }) {

			renderer.type = type;
			PrimitiveMeshGenerator::Generate(renderer, mesh);
			for (size_t index = 0; index < mesh.indices.size(); index += 3) {

				const PrimitiveMeshVertex& a = mesh.vertices[mesh.indices[index + 0]];
				const PrimitiveMeshVertex& b = mesh.vertices[mesh.indices[index + 1]];
				const PrimitiveMeshVertex& c = mesh.vertices[mesh.indices[index + 2]];
				const Vector3 face = Vector3::Cross(b.position - a.position, c.position - a.position);
				if (face.Length() <= 1e-6f) continue;
				const Vector3 normal = a.normal + b.normal + c.normal;
				// D3Dの時計回り面と頂点法線の向きを揃える
				if (Vector3::Dot(face, normal) >= 0.0f) return false;
			}
		}
		return true;
	}
}

bool NEMTests::TestPrimitiveTangents() {

	using namespace Engine;
	PrimitiveRendererComponent renderer;
	PrimitiveMeshData mesh;
	// 平面の軸とU反転に合わせて接線・従法線が向くことを確認する
	for (auto axis : { PrimitivePlaneAxis::XY, PrimitivePlaneAxis::XZ, PrimitivePlaneAxis::YZ }) {
		for (float sizeX : { 2.0f, -2.0f }) {
			renderer.plane.axis = axis;
			renderer.plane.size = Vector2(sizeX, 3.0f);
			renderer.plane.divideX = 2;
			renderer.plane.divideY = 3;
			PrimitiveMeshGenerator::Generate(renderer, mesh);
			if (mesh.vertices.empty()) return false;
			const Vector3 expectedTangent = axis == PrimitivePlaneAxis::YZ ?
				Vector3(0.0f, sizeX < 0.0f ? -1.0f : 1.0f, 0.0f) :
				Vector3(sizeX < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
			const Vector3 expectedBitangent = axis == PrimitivePlaneAxis::XY ?
				Vector3(0.0f, -1.0f, 0.0f) : Vector3(0.0f, 0.0f, -1.0f);
			for (const auto& vertex : mesh.vertices) {
				const Vector3 bitangent = Vector3::Cross(vertex.normal, vertex.tangent) * vertex.tangentSign;
				if (Vector3::Dot(vertex.tangent, expectedTangent) < 0.999f ||
					Vector3::Dot(bitangent, expectedBitangent) < 0.999f) return false;
			}
		}
	}
	// 大きさが0の形状でも有限な直交基底を返す
	renderer.plane.size = Vector2::AnyInit(0.0f);
	PrimitiveMeshGenerator::Generate(renderer, mesh);
	for (const auto& vertex : mesh.vertices) {
		if (!std::isfinite(vertex.tangent.x) || !std::isfinite(vertex.tangent.y) ||
			!std::isfinite(vertex.tangent.z) || std::fabs(Vector3::Dot(vertex.normal, vertex.tangent)) > 0.001f ||
			std::fabs(vertex.tangent.Length() - 1.0f) > 0.001f) return false;
	}
	return TestNormalizedGeometryKeys() && TestFiniteParameters() && TestGeometryKeyCollision() &&
		TestGeometryCapacity() && TestTriangleWinding() && TestRaytracingCullFlags() && TestImportedMeshWinding();
}

bool NEMTests::TestClothGeometryGPU() {

	using namespace Engine;
	GraphicsCore graphics;
	graphics.Init(false);
	const ScopedCleanup finalize([&]() noexcept { graphics.Finalize(); });
	PrimitiveGeometryManager manager;
	manager.Init(graphics);
	const ScopedCleanup release([&]() noexcept { graphics.GetDXObject().WaitForGPU(); manager.Clear(); });
	ECSWorld world;
	const std::array owners{world.CreateEntity(), world.CreateEntity()};
	for (Entity owner : owners) {
		auto& plane = world.AddComponent<PrimitiveRendererComponent>(owner);
		plane.plane.divideX = 12;
		plane.plane.divideY = 8;
		world.AddComponent<ClothComponent>(owner);
		auto& runtime = TryGetCloth(world, owner)->runtime;
		PrimitiveMeshGenerator::Generate(world.GetComponent<PrimitiveRendererComponent>(owner), runtime.mesh);
	}
	auto& platform = graphics.GetDXObject();
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 support{};
	const bool dxr = SUCCEEDED(platform.GetDevice()->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &support,
		sizeof(support))) && support.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED;
	std::array<std::array<ID3D12Resource*, kGraphicsFrameContextCount>, 2> vertexSlots{};
	std::array<ID3D12Resource*, 2> acceleration{};
	for (uint32_t frame = 1; frame <= 6; ++frame) {
		graphics.TickFrameServices();
		graphics.BeginRenderFrame();
		manager.BeginFrame();
		bool valid = true;
		for (size_t i = 0; i < owners.size(); ++i) {
			const auto& plane = world.GetComponent<PrimitiveRendererComponent>(owners[i]);
			auto& runtime = TryGetCloth(world, owners[i])->runtime;
			runtime.revision = frame;
			runtime.mesh.vertices[0].position.z = float(frame) * (i == 0 ? 0.25f : -0.25f);
			auto* geometry = manager.Resolve(graphics, plane, &world, owners[i]);
			if (!geometry || !geometry->clothVertices) { valid = false; break; }
			auto* resource = geometry->GetVertexResource();
			auto& previous = vertexSlots[i][GraphicsFrameState::GetCurrentIndex()];
			if (!resource || (previous && resource != previous)) { valid = false; break; }
			previous = resource;
			// 実際にShaderが参照するフレーム領域に各布の座標が入っているか確認する
			D3D12_RANGE first{0, sizeof(MeshVertex)};
			void* mapped = nullptr;
			if (FAILED(resource->Map(0, &first, &mapped))) { valid = false; break; }
			const float position = static_cast<MeshVertex*>(mapped)->position.z;
			D3D12_RANGE written{};
			resource->Unmap(0, &written);
			valid &= position == runtime.mesh.vertices[0].position.z;
			if (dxr) {
				valid &= manager.EnsureBLAS(platform.GetDevice(), platform.GetDxCommand()->GetCommandList(), *geometry);
				auto* blas = geometry->blas.GetResource();
				valid &= blas && (!acceleration[i] || acceleration[i] == blas);
				acceleration[i] = blas;
			}
		}
		const auto slot = GraphicsFrameState::GetCurrentIndex();
		valid &= vertexSlots[0][slot] != vertexSlots[1][slot];
		valid &= !dxr || acceleration[0] != acceleration[1];
		graphics.SubmitRenderFrame();
		graphics.EndRenderFrame();
		if (!valid) return false;
	}
	platform.WaitForGPU();
	return SUCCEEDED(platform.GetDevice()->GetDeviceRemovedReason()) && CheckSimulatedClothGeometry(graphics, manager, dxr);
}
