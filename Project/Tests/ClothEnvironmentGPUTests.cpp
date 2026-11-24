#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include "ParticleGPUFixture.h"
#include "TestFixtures.h"
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameUploadBufferAllocator.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Particle/ParticleFieldAssetCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Primitive/ClothGPUEnvironment.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Physics/ClothCollision.h>
#include <Engine/Core/World/Systems/Physics/ClothWind.h>

// c++
#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>
#include <iostream>

using namespace Engine;

namespace {

	struct ClothProbe {
		Vector3 position;
		uint32_t mask;
		Vector3 velocity;
		uint32_t mode, fields, colliders, count;
		float thickness;
	};
	static_assert(sizeof(ClothProbe) == 48);

	bool CheckVectorAtlas() {

		NEMTests::TestDirectory directory("ClothGPUVectorAtlas", RuntimePaths::GetGameAssetsRoot());
		const auto path = directory.GetPath() / "wind.fga";
		const auto save = [&](float speed) {
			std::ofstream(path) << "1,1,1, -1,-1,-1, 1,1,1, " << speed << ",0,0,";
		};
		save(1);
		AssetDatabase database;
		const auto id = database.ImportOrGet(RuntimePaths::ToAssetPath(path), AssetType::DefaultAsset);
		if (!id) return false;
		GraphicsCore graphics;
		graphics.Init(false);
		const ScopedCleanup finalize([&]() noexcept { graphics.Finalize(); });
		FrameUploadBufferAllocator uploads;
		ECSWorld world;
		for (uint32_t i = 0; i < 2; ++i) {
			auto entity = world.CreateEntity();
			world.AddComponent<TransformComponent>(entity);
			auto& field = world.AddComponent<VectorFieldComponent>(entity);
			field.preset = ParticleVectorFieldPreset::Asset;
			field.field = id;
		}
		std::unique_ptr<ParticleFieldAssetCache> assets;
		std::shared_ptr<ClothGPUVectorAtlas> atlas;
		ClothGPUEnvironment initial, next;
		const auto waitForChange = [&](ClothGPUEnvironment& environment, const std::shared_ptr<ClothGPUVectorAtlas>& previous) {
			const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			do {
				environment.Collect(world, &database, assets, atlas, true, false);
				if (atlas && atlas != previous && environment.fields.size() == 2) return true;
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			} while (std::chrono::steady_clock::now() < deadline);
			return false;
		};
		if (!waitForChange(initial, {})) return false;
		const auto original = atlas;
		if (initial.fields[0].grid[3] != initial.fields[1].grid[3]) return false;
		const auto address = initial.GetGPUAddresses(graphics, uploads).samples;
		next.Collect(world, &database, assets, atlas, true, false);
		if (!address || atlas != original || address != next.GetGPUAddresses(graphics, uploads).samples) return false;
		save(3);
		database.NotifyContentChanged(id);
		if (!waitForChange(next, original)) return false;
		const auto replacement = next.GetGPUAddresses(graphics, uploads).samples;
		if (!replacement || address == replacement || initial.GetGPUAddresses(graphics, uploads).samples != address) return false;
		auto& platform = graphics.GetDXObject();
		platform.WaitForGPU();
		return SUCCEEDED(platform.GetDevice()->GetDeviceRemovedReason());
	}
}

bool NEMTests::TestClothEnvironmentGPU() {

	if (!CheckVectorAtlas()) {
		std::cerr << "Cloth FGA atlas reuse or invalidation failed\\n";
		return false;
	}
	ECSWorld world;
	const auto create = [&]() {
		const auto entity = world.CreateEntity();
		auto& t = world.AddComponent<TransformComponent>(entity);
		t.localPos = {1, -2, 4};
		t.localRotation = Quaternion::FromEulerDegrees({12, -40, 22});
		t.localScale = {1, 1.25f, 1.5f};
		return entity;
	};
	for (uint32_t i = 0; i < 4; ++i) {
		auto& component = world.AddComponent<ForceFieldComponent>(create());
		component.type = static_cast<ParticleForceFieldType>(i);
		component.shape = static_cast<ParticleFieldShape>(i % 3);
		component.strength = 3.25f;
		component.range = 1;
		component.size = {1, 2, 2};
		component.layerMask = 1u << i;
	}
	for (uint32_t i = 0; i < 2; ++i) {
		auto& component = world.AddComponent<VectorFieldComponent>(create());
		component.preset = static_cast<ParticleVectorFieldPreset>(i);
		component.intensity = 2;
		component.tightness = i ? 0.6f : 0;
		component.layerMask = 16u << i;
	}
	const ColliderShapeType shapes[]{ColliderShapeType::Sphere3D, ColliderShapeType::AABB3D, ColliderShapeType::OBB3D, ColliderShapeType::Capsule3D};
	for (uint32_t i = 0; i < 4; ++i) {
		auto& collision = world.AddComponent<CollisionComponent>(create());
		collision.shape.type = shapes[i];
		collision.typeMask = 1u << i;
	}
	ClothGPUEnvironment environment;
	std::unique_ptr<ParticleFieldAssetCache> assets;
	std::shared_ptr<ClothGPUVectorAtlas> atlas;
	environment.Collect(world, nullptr, assets, atlas, true, true);
	auto cpuFields = ClothWind::CollectWind(world, nullptr, assets);
	const auto cpuColliders = ClothCollisionQuery::Collect(world);
	if (environment.fields.size() != 6 || environment.colliders.size() != 4 || cpuFields.empty()) return false;

	// 三線形補間では各軸の格子位置に異なるベクトルを入れて検証する
	auto grid = std::make_shared<VectorFieldData>();
	grid->resolution = {2, 2, 2};
	grid->minimum = {-5, -2, 1};
	grid->maximum = {7, 3, 9};
	for (int i = 0; i < 8; ++i) grid->vectors.emplace_back(float(i & 1), float((i >> 1) & 1), float((i >> 2) & 1));
	auto fga = cpuFields.back();
	fga.preset = ParticleVectorFieldPreset::Asset;
	fga.grid = grid;
	fga.mask = 64;
	cpuFields.push_back(fga);
	auto packed = environment.fields.back();
	packed.kind = 6;
	packed.mask = 64;
	packed.grid[0] = packed.grid[1] = packed.grid[2] = 2;
	packed.grid[3] = 0;
	environment.fields.push_back(packed);

	std::vector<ClothProbe> probes;
	std::vector<Vector3> expected;
	for (uint32_t mode = 0; mode < 2; ++mode) {
		for (uint32_t mask : {0u, 1u, 2u, 4u, 8u, 16u, 32u, 64u, UINT32_MAX}) {
			std::vector<CollisionShapeInstance> selected;
			ClothCollisionQuery::Select(cpuColliders, mask, selected);
			for (const auto& point : {Vector3(1, -2, 4), Vector3(1.12f, -2.15f, 4.33f), Vector3(1.8f, -1.8f, 4.6f), Vector3(9, 5, -1)}) {
				ClothProbe probe{point, mask, {2, -3, 1}, mode, static_cast<uint32_t>(environment.fields.size()),
					static_cast<uint32_t>(environment.colliders.size()), 0, 0.05f};
				probes.push_back(probe);
				Vector3 value{};
				if (mode == 0) {
					for (const auto& field : cpuFields)
						if (field.mask & mask) value += ClothWind::SampleForce(field, point, probe.velocity, 1.0f / 120);
				} else {
					value = point;
					ClothCollisionQuery::Project(selected, value, probe.thickness);
				}
				expected.push_back(value);
			}
		}
	}
	probes.front().count = static_cast<uint32_t>(probes.size());

	ParticleGPUFixture fixture;
	auto pipeline = fixture.MakePipeline("../../../../../../Tests/Shaders/clothEnvironmentValidation.CS.hlsl");
	if (!pipeline) return false;
	auto* commands = fixture.commands.GetCommandList();
	commands->SetComputeRootSignature(pipeline->GetRootSignature());
	commands->SetPipelineState(pipeline->GetComputePipeline());
	StructuredInstanceBuffer<ClothGPUWindField> fields;
	StructuredInstanceBuffer<Vector3> samples;
	StructuredInstanceBuffer<ClothGPUCollider> colliders;
	StructuredInstanceBuffer<ClothProbe> input;
	const auto bind = [&](const char* name, auto& buffer, const auto& data) {
		buffer.Init(fixture.device.Get(), &fixture.descriptors);
		buffer.Upload(data);
		RootBindingCommand::SetComputeSRV(commands, pipeline->FindBindingByName(name, ShaderBindingKind::SRV),
			buffer.GetGPUAddress(), buffer.GetGPUHandle());
	};
	bind("gClothFields", fields, environment.fields);
	bind("gClothSamples", samples, grid->vectors);
	bind("gClothColliders", colliders, environment.colliders);
	bind("gClothProbes", input, probes);
	StructuredRWBuffer<Vector3> results;
	results.Init(fixture.device.Get(), &fixture.descriptors);
	results.EnsureCapacity(static_cast<uint32_t>(probes.size()));
	results.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	RootBindingCommand::SetComputeUAV(commands, pipeline->FindBindingByName("gClothResults", ShaderBindingKind::UAV),
		results.GetGPUAddress(), results.GetUAVGPUHandle());
	commands->Dispatch(static_cast<uint32_t>((probes.size() + 63) / 64), 1, 1);
	fixture.commands.UAVBarrierAll();
	results.Transition(fixture.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
	const auto bytes = fixture.ReadResource(results.GetResource(), probes.size() * sizeof(Vector3));
	for (size_t i = 0; i < expected.size(); ++i) {
		Vector3 actual;
		std::memcpy(&actual, bytes.data() + i * sizeof(actual), sizeof(actual));
		if (!((actual - expected[i]).Length() <= 0.002f)) {
			std::cerr << "Cloth GPU environment mismatch, probe " << i << " actual " << actual.x << ',' << actual.y << ',' << actual.z
				<< " expected " << expected[i].x << ',' << expected[i].y << ',' << expected[i].z << '\n';
			return false;
		}
	}
	return fixture.CheckMessages();
}
