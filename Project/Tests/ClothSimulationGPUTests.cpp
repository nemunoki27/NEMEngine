#include "TestContracts.h"

#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Primitive/ClothGPUSolver.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Physics/ClothComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/ParticleFieldComponents.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Physics/ClothSystem.h>

#include <chrono>
#include <cmath>
#include <iostream>

namespace {

	using namespace Engine;
	using Clock = std::chrono::steady_clock;

	Entity AddCloth(ECSWorld& world, int nx, int ny, int index) {

		const auto owner = world.CreateEntity();
		world.AddComponent<TransformComponent>(owner);
		auto& plane = world.AddComponent<PrimitiveRendererComponent>(owner).plane;
		plane.axis = PrimitivePlaneAxis::XY;
		plane.size = {2.2f, 1.6f};
		plane.divideX = nx;
		plane.divideY = ny;
		world.AddComponent<ClothComponent>(owner);
		ClothSettings settings;
		settings.edge = index % 2 ? ClothEdge::Top : ClothEdge::Left;
		settings.playInEditMode = true;
		settings.gravityScale = 0.55f;
		settings.windDirection = {1, 0.15f, 0.44f};
		settings.windStrength = 9;
		settings.receiveSceneWind = true;
		SetClothSettings(world, owner, settings);
		return owner;
	}

	bool CompareMesh(const ClothRuntime& cpu, const MeshVertex* vertices) {

		float error = 0.0f, motion = 0.0f, maxStretch = 0.0f, cpuStretch = 0.0f;
		for (size_t i = 0; i < cpu.rest.size(); ++i) {
			const auto& v = vertices[i];
			const Vector3 position(v.position.x, v.position.y, v.position.z);
			const float difference = (cpu.mesh.vertices[i].position - position).Length();
			if (!std::isfinite(difference) || !std::isfinite(v.normal.Length()) ||
				std::abs(v.normal.Length() - 1) > 0.002f || std::abs(Vector3::Dot(v.normal, v.tangent)) > 0.002f) return false;
			if (cpu.pinned[i] && difference > 0.0001f) return false;
			if (position.y < -0.886f) return false;
			error += difference;
			motion = (std::max)(motion, (cpu.rest[i] - position).Length());
		}
		for (const auto& link : cpu.constraints) {
			if (link.kind != 0) continue;
			const auto& a = vertices[link.a].position;
			const auto& b = vertices[link.b].position;
			maxStretch = (std::max)(maxStretch, Vector3(a.x-b.x, a.y-b.y, a.z-b.z).Length() - link.restLength);
			cpuStretch = (std::max)(cpuStretch, (cpu.positions[link.a] - cpu.positions[link.b]).Length() - link.restLength);
		}
		std::cout << " edgeStretch=" << maxStretch << " cpuEdgeStretch=" << cpuStretch;
		std::cout << " meanPositionError=" << error / cpu.rest.size() << " motion=" << motion;
		return maxStretch < cpuStretch + 0.05f && error / cpu.rest.size() < 0.18f && motion > 0.01f;
	}

	bool CompareSolvers(GraphicsCore& graphics, RenderAssetLibrary& assets, PipelineStateCache& pipelines,
		int nx, int ny, int count) {

		auto& platform = graphics.GetDXObject();
		ECSWorld cpu, gpu;
		ClothSystem cpuSystem, gpuSystem;
		std::vector<Entity> cpuOwners, gpuOwners;
		for (int i = 0; i < count; ++i) {
			cpuOwners.push_back(AddCloth(cpu, nx, ny, i));
			gpuOwners.push_back(AddCloth(gpu, nx, ny, i));
		}
		std::vector<Entity> emitters;
		for (auto* world : {&cpu, &gpu}) {
			const auto floor = world->CreateEntity();
			world->AddComponent<TransformComponent>(floor).localPos = {0, -1.1f, 0};
			auto& shape = world->AddComponent<CollisionComponent>(floor).shape;
			shape.type = ColliderShapeType::AABB3D;
			shape.halfExtents3D = {5, 0.2f, 5};
			const auto source = world->CreateEntity();
			emitters.push_back(source);
			world->AddComponent<TransformComponent>(source);
			auto& field = world->AddComponent<ForceFieldComponent>(source);
			field.type = ParticleForceFieldType::Wind;
			field.axis = {0, 0, 1};
			field.strength = 4;
		}
		SystemContext cpuContext, gpuContext;
		cpuContext.mode = gpuContext.mode = WorldMode::Play;
		cpuContext.deltaTime = gpuContext.deltaTime = 1.0f / 60.0f;
		gpuContext.graphicsPlatform = &platform;
		gpuContext.renderAssetLibrary = &assets;
		constexpr int frames = 18, warmup = 4;
		const size_t meshBytes = sizeof(MeshVertex) * (nx + 1) * (ny + 1);
		ComPtr<ID3D12Resource> readback, times;
		DxUtils::CreateReadbackBufferResource(platform.GetDevice(), readback, meshBytes * count);
		DxUtils::CreateReadbackBufferResource(platform.GetDevice(), times, frames * 2 * sizeof(uint64_t));
		D3D12_QUERY_HEAP_DESC queryDesc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, frames * 2, 0};
		ComPtr<ID3D12QueryHeap> queries;
		if (FAILED(platform.GetDevice()->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(queries.GetAddressOf())))) return false;
		uint64_t frequency = 0;
		if (FAILED(platform.GetCommandQueue()->GetQueue()->GetTimestampFrequency(&frequency)) || !frequency) return false;
		double cpuMs = 0, submitMs = 0, frameMs = 0;
		bool valid = true;
		for (int frame = 0; frame < frames; ++frame) {
			const Vector3 fieldPosition(frame < warmup || frame >= frames - warmup ? 20.0f : 0.0f, 0, 0);
			cpu.GetComponent<TransformComponent>(emitters[0]).localPos = fieldPosition;
			gpu.GetComponent<TransformComponent>(emitters[1]).localPos = fieldPosition;
			const auto start = Clock::now();
			cpuSystem.Update(cpu, cpuContext);
			const auto gpuStart = Clock::now();
			gpuSystem.Update(gpu, gpuContext);
			graphics.TickFrameServices();
			graphics.BeginRenderFrame();
			auto* cmd = platform.GetDxCommand()->GetCommandList();
			platform.GetDxCommand()->SetDescriptorHeaps({graphics.GetSRVDescriptor().GetDescriptorHeap()});
			cmd->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame * 2);
			ClothSystem::DispatchGPU(gpu, graphics, assets, pipelines);
			cmd->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame * 2 + 1);
			cmd->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, frame * 2, 2, times.Get(), frame * 16);
			const auto dispatched = Clock::now();
			if (frame == frames - 1) {
				for (int i = 0; i < count; ++i) {
					const auto& runtime = TryGetCloth(gpu, gpuOwners[i])->runtime;
					auto* resource = runtime.gpu ? runtime.gpu->GetVertexResource() : nullptr;
					if (!resource) { valid = false; break; }
					const auto state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
					D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, state, D3D12_RESOURCE_STATE_COPY_SOURCE);
					cmd->ResourceBarrier(1, &barrier);
					cmd->CopyBufferRegion(readback.Get(), i * meshBytes, resource, 0, meshBytes);
					std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
					cmd->ResourceBarrier(1, &barrier);
				}
			}
			graphics.SubmitRenderFrame();
			graphics.EndRenderFrame();
			if (frame >= warmup) {
				const auto ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
				cpuMs += ms(gpuStart - start);
				submitMs += ms(dispatched - gpuStart);
				frameMs += ms(Clock::now() - gpuStart);
			}
		}
		platform.WaitForGPU();
		if (!valid) return false;
		void* data = nullptr;
		if (FAILED(times->Map(0, nullptr, &data))) return false;
		double gpuMs = 0;
		auto* timestamps = static_cast<const uint64_t*>(data);
		for (int i = warmup; i < frames; ++i) gpuMs += (timestamps[2*i+1] - timestamps[2*i]) * 1000.0 / frequency;
		const D3D12_RANGE written{};
		times->Unmap(0, &written);
		const int samples = frames - warmup;
		std::cout << "\nCloth " << nx << "x" << ny << " count=" << count << " CPUsolverMs=" << cpuMs / samples
			<< " GPUrecordWithBeginMs=" << submitMs / samples << " GPUsolverMs=" << gpuMs / samples
			<< " GPUframeMs=" << frameMs / samples;
		if (FAILED(readback->Map(0, nullptr, &data))) return false;
		for (int i = 0; i < count; ++i) {
			const auto* mesh = reinterpret_cast<const MeshVertex*>(static_cast<const uint8_t*>(data) + meshBytes * i);
			valid &= CompareMesh(TryGetCloth(cpu, cpuOwners[i])->runtime, mesh);
		}
		readback->Unmap(0, &written);
		std::cout << '\n';
		return valid && SUCCEEDED(platform.GetDevice()->GetDeviceRemovedReason());
	}
}

bool NEMTests::TestClothSimulationGPU() {

	GraphicsCore graphics;
	graphics.Init(false);
	const ScopedCleanup shutdown([&]() noexcept { graphics.Finalize(); });
	AssetDatabase database;
	database.Init();
	if (!database.RebuildMeta({RuntimePaths::GetEngineAssetsRoot() / "Rendering/Builtin/Cloth"})) return false;
	RenderAssetLibrary assets;
	assets.Init(&database);
	PipelineStateCache pipelines;
	return CompareSolvers(graphics, assets, pipelines, 24, 16, 1) &&
		CompareSolvers(graphics, assets, pipelines, 64, 48, 1) && CompareSolvers(graphics, assets, pipelines, 24, 16, 8);
}
