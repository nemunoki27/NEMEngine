#include "ShaderGraphNodePreviewCases.h"
#include "ShaderGraphNodePreviewGPUFixture.h"
#include "TestImage.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphNodeRegistry.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/ScopedCleanup.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
// directX
#include <DirectXTex.h>

using namespace Engine;

bool NEMTests::WriteShaderGraphNodePreviewImages() {

	constexpr uint32_t kImageSize = 128;
	const auto directory = RuntimePaths::GetEngineProjectRoot().parent_path() / "LocalWork/Verification/ShaderGraphVerification/Images";
	std::filesystem::create_directories(directory);
	ShaderGraphNodePreviewGPUFixture fixture;
	// 識別子の異なるGraphでも既定の指数演算を同じ値へ評価する
	for (uint32_t iteration = 0; iteration < 32; ++iteration) {
		const auto testCase = CreateShaderGraphNodePreviewCase(ShaderGraphNodeKind::Exponential);
		const auto compiled = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID);
		const auto pixels = fixture.Render(compiled);
		if (std::any_of(pixels.begin(), pixels.end(), [](const Vector4& pixel) { return pixel != Vector4(1, 1, 1, 1); })) {
			std::ofstream(directory / "Exponential.PS.hlsl") << compiled.pixelHLSL;
			std::ofstream(directory / "Exponential.graph.json") << ToJson(testCase.graph).dump(2);
			std::cerr << "Exponential default iteration=" << iteration << " result=" << pixels.front().x << ',' <<
				pixels.front().y << ',' << pixels.front().z << '\n';
			return false;
		}
	}
	nlohmann::json catalog = nlohmann::json::array();
	for (const auto& descriptor : ShaderGraphNodeRegistry::GetDescriptors()) {
		if (!descriptor.preview) continue;
		const auto testCase = CreateShaderGraphNodePreviewCase(descriptor.kind);
		const auto count = GetShaderGraphOutputCount(testCase.graph.nodes.front());
		for (uint32_t slot = 0; slot < count; ++slot) {
			ShaderGraphNodePreviewOptions options;
			auto compiled = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID, slot, options, testCase.resolver);
			if (compiled.usesGeometry) {
				options.sphere = true;
				compiled = ShaderGraphNodePreviewCompiler::Compile(testCase.graph, testCase.nodeID, slot, options, testCase.resolver);
			}
			if (!compiled.Succeeded()) {
				std::cerr << "Preview image compilation: " << descriptor.name << " output=" << slot << '\n';
				for (const auto& diagnostic : compiled.diagnostics) std::cerr << diagnostic.message << '\n';
				return false;
			}
			auto pixels = fixture.Render(compiled, 1.25f, .02f, .03f, nullptr, kImageSize, {0, 0, 0, 0});
			if (descriptor.kind == ShaderGraphNodeKind::Exponential &&
				std::any_of(pixels.begin(), pixels.end(), [](const Vector4& pixel) { return pixel != Vector4(1, 1, 1, 1); })) {
				std::ofstream(directory / "Exponential.PS.hlsl") << compiled.pixelHLSL;
				std::cerr << "Exponential default result=" << pixels.front().x << ',' << pixels.front().y << ',' << pixels.front().z << '\n';
				return false;
			}
			if (pixels.size() != kImageSize * kImageSize || std::any_of(pixels.begin(), pixels.end(), [](const Vector4& pixel) {
				return !std::isfinite(pixel.x) || !std::isfinite(pixel.y) || !std::isfinite(pixel.z) ||
					!std::isfinite(pixel.w) || pixel.x < 0 || 1 < pixel.x || pixel.y < 0 || 1 < pixel.y ||
					pixel.z < 0 || 1 < pixel.z || pixel.w < 0 || 1 < pixel.w;
			})) return false;
			const std::string file = std::to_string(static_cast<uint32_t>(descriptor.kind)) + "-" + std::to_string(slot) + ".png";
			const DirectX::Image image{kImageSize, kImageSize, DXGI_FORMAT_R32G32B32A32_FLOAT,
				kImageSize * sizeof(Vector4), pixels.size() * sizeof(Vector4),
				reinterpret_cast<uint8_t*>(pixels.data())};
			if (!SaveTestImage(image, directory / file)) return false;
			catalog.push_back({{"name", descriptor.name}, {"output", slot}, {"file", file}, {"sphere", options.sphere}});
		}
	}
	std::ofstream manifest(directory / "catalog.json", std::ios::binary);
	manifest << catalog.dump(2);
	if (!manifest) return false;
	std::cout << "Node preview images: " << catalog.size() << " outputs, 128x128, " << directory << '\n';
	return fixture.CheckMessages();
}

bool NEMTests::TestMaterialPreviewGPU() {

	AssetDatabase database;
	database.Init();
	const auto glassDir = RuntimePaths::GetGameAssetsRoot() / "Materials/Glass";
	const auto shapes = RuntimePaths::GetEngineAssetsRoot() / "Models/Editor/MaterialPreview";
	if (!database.RebuildMeta({RuntimePaths::GetEngineAssetsRoot() / "Rendering", shapes, glassDir})) return false;
	const auto* glassMeta = database.FindByPath(RuntimePaths::ToAssetPath(glassDir / "Glass.material.json"));
	const auto* sphere = database.FindByPath(RuntimePaths::ToAssetPath(shapes / "Sphere.gltf"));
	if (!glassMeta || !sphere) return false;
	GraphicsCore graphics;
	graphics.Init(false);
	const ScopedCleanup releaseGraphics([&]() noexcept { graphics.Finalize(); });
	RenderPipelineRunner runner;
	runner.Init();
	MultiRenderTarget output;
	const ScopedCleanup releasePipeline([&]() noexcept {
		graphics.GetDXObject().WaitForGPU();
		runner.Finalize();
		output.Destroy();
	});
	constexpr uint32_t size = 128;
	MultiRenderTargetCreateDesc desc{};
	desc.width = desc.height = size;
	desc.colors.push_back({.name = "MaterialPreviewImage", .format = DXGI_FORMAT_R16G16B16A16_FLOAT});
	desc.depth = DepthTextureCreateDesc{.width = size, .height = size};
	output.Create(graphics.GetDXObject().GetDevice(), &graphics.GetRTVDescriptor(), &graphics.GetDSVDescriptor(),
		&graphics.GetSRVDescriptor(), desc);
	ECSWorld world;
	const Entity subject = world.CreateEntity();
	world.AddComponent<TransformComponent>(subject);
	auto& mesh = world.AddComponent<MeshRendererComponent>(subject);
	mesh.mesh = sphere->guid;
	const Entity light = world.CreateEntity();
	world.AddComponent<TransformComponent>(light);
	auto& directional = world.AddComponent<DirectionalLightComponent>(light);
	directional.direction = Vector3(0.3f, -0.4f, 1.0f).Normalize();
	directional.shadowStrength = 0.0f;
	for (int i = -6; i <= 6; ++i) {
		const Entity stripe = world.CreateEntity();
		world.AddComponent<HierarchyComponent>(stripe).parent = subject;
		auto& transform = world.AddComponent<TransformComponent>(stripe);
		transform.localPos = {0.3f * i, 0.0f, 2.0f};
		transform.worldMatrix = MakeLocalMatrix(transform);
		auto& block = world.AddComponent<PrimitiveRendererComponent>(stripe);
		block.type = PrimitiveType::Cube;
		block.cube.size = {0.3f, 4.0f, 0.1f};
		block.materialInstance.Set(MaterialParameterIDs::BaseColor, MaterialParameterNames::BaseColor,
			MaterialParameterSemantic::BaseColor, {.value = i % 2 ? Color4::White() : Color4(0.05f, 0.08f, 0.1f, 1.0f)});
	}
	SystemContext system{};
	system.world = &world;
	system.assetDatabase = &database;
	EntityPreviewRenderRequest request{};
	request.world = &world;
	request.rootEntity = subject;
	request.assetDatabase = &database;
	request.surface = &output;
	request.materialPreview = true;
	request.systemContext = &system;
	request.camera.transform3D.pos = {0, 0, -4};
	request.camera.perspectiveFovY = 38.0f;
	const auto capture = [&](const char* label, std::vector<uint8_t>& pixels) {
		bool ready = false;
		for (int i = 0; i < 240 && !ready; ++i) {
			graphics.TickFrameServices();
			graphics.BeginRenderFrame();
			RenderFrameRequest frame{};
			frame.assetDatabase = &database;
			runner.Render(graphics, frame);
			ready = runner.RenderEntityPreview(graphics, request);
			graphics.SubmitRenderFrame();
			graphics.EndRenderFrame();
		}
		if (!ready) return false;
		auto* texture = output.GetColorTexture(0);
		DirectX::ScratchImage image, rgba;
		const auto state = texture->GetCurrentState();
		if (FAILED(DirectX::CaptureTexture(graphics.GetDXObject().GetCommandQueue()->GetQueue(), texture->GetResource(),
			false, image, state, state)) || FAILED(DirectX::Convert(*image.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM,
			DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, rgba))) return false;
		const auto* result = rgba.GetImage(0, 0, 0);
		pixels.assign(result->pixels, result->pixels + result->slicePitch);
		const auto directory = RuntimePaths::GetEngineProjectRoot().parent_path() / "LocalWork/Verification/MaterialInspector";
		std::filesystem::create_directories(directory);
		return SaveTestImage(*result, directory / (std::string(label) + ".png"));
	};
	std::vector<uint8_t> opaque, on, off;
	if (!capture("opaque", opaque)) return false;
	auto& library = runner.GetRenderAssetLibrary();
	const auto* source = library.LoadMaterial(glassMeta->guid);
	if (!source) return false;
	MaterialAsset glass = *source;
	const auto records = glass.parameters.GetRecords();
	const auto toggle = std::find_if(records.begin(), records.end(), [](const auto& record) {
		return record.namedValue.first == "屈折";
	});
	if (toggle == records.end()) return false;
	const auto toggleID = toggle->id;
	world.GetComponent<MeshRendererComponent>(subject).material = glass.guid;
	world.MarkComponentModified<MeshRendererComponent>(subject);
	for (bool enabled : {true, false}) {
		glass.parameters.Set(toggleID, "屈折", MaterialParameterSemantic::None, {.value = enabled});
		library.RegisterDerivedMaterial(glass);
		if (!capture(enabled ? "glass-on" : "glass-off", enabled ? on : off)) return false;
	}
	unsigned refractionPixels = 0, surfacePixels = 0;
	for (uint32_t y = size / 4; y < size * 3 / 4; ++y) {
		for (uint32_t x = size / 4; x < size * 3 / 4; ++x) {
			const auto pixel = 4 * (size * y + x);
			if (std::abs(int(on[pixel]) - int(off[pixel])) > 2) ++refractionPixels;
			if (std::abs(int(opaque[pixel]) - int(off[pixel])) > 2) ++surfacePixels;
		}
	}
	std::cout << "Material preview changed pixels: surface=" << surfacePixels << " refraction=" << refractionPixels << '\n';
	if (surfacePixels <= 100 || refractionPixels <= 20) return false;
	world.GetComponent<MeshRendererComponent>(subject).material = {};
	world.MarkComponentModified<MeshRendererComponent>(subject);
	const Entity projector = world.CreateEntity();
	world.AddComponent<TransformComponent>(projector);
	auto& decal = world.AddComponent<DecalProjectorComponent>(projector);
	decal.size = {1.5f, 1.5f, 2.2f};
	decal.materialInstance.Set("color", MaterialParameterValue{.value = Color4(1, 0, 0, 1)});
	std::vector<uint8_t> projected, sprite;
	if (!capture("decal", projected)) return false;
	const size_t center = 4 * (size / 2 + size * size / 2);
	if (int(projected[center + 1]) + 8 >= int(opaque[center + 1])) return false;
	const Entity overlay = world.CreateEntity();
	world.AddComponent<TransformComponent>(overlay);
	world.AddComponent<HierarchyComponent>(overlay).parent = subject;
	auto& quad = world.AddComponent<SpriteRendererComponent>(overlay);
	quad.size = {48.0f, 48.0f};
	quad.materialInstance.Set(MaterialParameterIDs::BaseColor, MaterialParameterNames::BaseColor,
		MaterialParameterSemantic::BaseColor, {.value = Color4(0, 1, 0, 1)});
	request.camera.enableOrthographic = true;
	request.camera.transform2D.pos = {-64, -64, 0};
	if (!capture("sprite", sprite)) return false;
	return int(sprite[center]) + 32 < int(sprite[center + 1]) && int(sprite[center + 2]) + 32 < int(sprite[center + 1]);
}
