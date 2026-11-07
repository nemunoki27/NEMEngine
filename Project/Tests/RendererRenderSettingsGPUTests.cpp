#include "RendererRenderSettingsGPUTests.h"
#include "ParticleGPUFixture.h"
#include "GPUFilterTestFixture.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/RendererPipelineSettings.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTexture2D.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthTexture2D.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RendererDepthAttachments.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/DepthTextureSnapshot.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxRenderTargetView.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxDepthStencilView.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <Engine/Core/Foundation/Math/Vector4.h>

// c++
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
// directX
#include <Externals/DirectX12/d3dx12.h>

namespace {

	using namespace Engine;
	constexpr uint32_t kSize = 32;

	struct Draw {

		RendererRenderSettings settings{};
		float depth = 0.5f;
		bool reverse = false;
		bool clearColor = false;
		Vector4 color = Vector4(1, 0, 0, 1);
	};

	struct Constants {

		Vector4 color{};
		float depth = 0.5f;
		uint32_t reverse = 0;
		float padding[2]{};
	};
	static_assert(sizeof(Constants) == 32);

	struct Result {

		Vector4 center{};
		uint32_t covered = 0;
	};

	// 本番の描画状態変換を使って小さい三角形を描画する
	class RasterFixture {
	public:

		RasterFixture() : directory_("RendererRenderSettingsGPU") {

			const auto shader = directory_.GetPath() / "test.hlsl";
			std::ofstream file(shader);
			file << "cbuffer Settings : register(b0) { float4 color; float depth; uint reverse; float2 pad; };\n"
				"float4 VS(uint id : SV_VertexID) : SV_POSITION { float2 p[3]={float2(-.8,-.8),float2(0,.8),float2(.8,-.8)};"
				"return float4(p[reverse != 0 ? 2-id : id],depth,1); }\n"
				"float4 PS() : SV_TARGET { return color; }";
			file.close();
			path_ = Algorithm::PathToUTF8(shader);
			targets_.Init(gpu_.device.Get(), {D3D12_DESCRIPTOR_HEAP_TYPE_RTV});
			targets_.SetRetirementQueue(gpu_.retirement);
			depths_.Init(gpu_.device.Get(), {D3D12_DESCRIPTOR_HEAP_TYPE_DSV});
			depths_.SetRetirementQueue(gpu_.retirement);
			color_.Create(gpu_.device.Get(), &targets_, &gpu_.descriptors,
				{.width = kSize, .height = kSize, .format = DXGI_FORMAT_R32G32B32A32_FLOAT});
			depth_.Create(&depths_, &gpu_.descriptors, {.width = kSize, .height = kSize});
		}

		~RasterFixture() {

			gpu_.Finish();
			color_.Destroy();
			depth_.Destroy();
			gpu_.Finish();
		}

		Result Render(std::span<const Draw> draws, float clearDepth = 1.0f, uint8_t clearStencil = 0,
			DepthTexture2D* externalDepth = nullptr) {

			color_.Transition(gpu_.commands, D3D12_RESOURCE_STATE_RENDER_TARGET);
			auto& depthTexture = externalDepth ? *externalDepth : depth_;
			depthTexture.Transition(gpu_.commands, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			auto* commands = gpu_.commands.GetCommandList();
			const auto target = color_.GetRenderTarget().rtvHandle;
			const auto depth = depthTexture.GetDSVCPUHandle();
			const float clear[4]{};
			commands->ClearRenderTargetView(target, clear, 0, nullptr);
			if (!externalDepth) {
				commands->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
					clearDepth, clearStencil, 0, nullptr);
			}
			commands->OMSetRenderTargets(1, &target, FALSE, &depth);
			const D3D12_VIEWPORT viewport{0, 0, kSize, kSize, 0, 1};
			const D3D12_RECT rectangle{0, 0, kSize, kSize};
			commands->RSSetViewports(1, &viewport);
			commands->RSSetScissorRects(1, &rectangle);
			commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			std::vector<std::unique_ptr<PipelineState>> pipelines;
			for (const auto& draw : draws) {
				GraphicsPipelineDesc desc;
				desc.preRaster = {.file = path_, .entry = "VS", .profile = "vs_6_0"};
				desc.pixel = {.file = path_, .entry = "PS", .profile = "ps_6_0"};
				desc.rasterizer = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
				desc.rasterizer.CullMode = D3D12_CULL_MODE_NONE;
				desc.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
				desc.depthStencil.DepthEnable = FALSE;
				desc.depthStencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
				desc.dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
				desc.rtvFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
				desc.numRenderTargets = 1;
				desc.disableNormalBlending = true;
				ApplyRendererPipelineSettings(desc, NormalizeRendererPipelineSettings(&draw.settings));
				auto pipeline = PipelineStateBuilder::CreateGraphics(gpu_.retirement, gpu_.device.Get(), &gpu_.compiler, desc);
				if (!pipeline) throw std::runtime_error("Renderer settings pipeline creation failed");
				if (draw.clearColor) commands->ClearRenderTargetView(target, clear, 0, nullptr);
				commands->SetGraphicsRootSignature(pipeline->GetRootSignature());
				commands->SetPipelineState(pipeline->GetGraphicsPipeline(BlendMode::Normal));
				commands->OMSetStencilRef(draw.settings.stencilReference);
				const Constants values{draw.color, draw.depth, draw.reverse ? 1u : 0u};
				const auto allocation = gpu_.constants.AllocateAndUpload(gpu_.retirement, gpu_.device.Get(), values);
				RootBindingCommand::SetGraphicsCBV(commands,
					pipeline->FindBindingByName("Settings", ShaderBindingKind::CBV), allocation.gpuAddress);
				commands->DrawInstanced(3, 1, 0, 0);
				pipelines.push_back(std::move(pipeline));
			}
			color_.Transition(gpu_.commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
			const auto resourceDesc = color_.GetResource()->GetDesc();
			D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
			UINT64 bytes = 0;
			gpu_.device.Get()->GetCopyableFootprints(&resourceDesc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
			ComPtr<ID3D12Resource> readback;
			DxUtils::CreateReadbackBufferResource(gpu_.device.Get(), readback, bytes);
			D3D12_TEXTURE_COPY_LOCATION destination{readback.Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
			destination.PlacedFootprint = footprint;
			D3D12_TEXTURE_COPY_LOCATION source{color_.GetResource(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
			commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
			gpu_.Finish();
			void* mapped = nullptr;
			const D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
			if (FAILED(readback->Map(0, &range, &mapped))) throw std::runtime_error("Renderer settings readback failed");
			Result result;
			for (uint32_t y = 0; y < kSize; ++y) {
				for (uint32_t x = 0; x < kSize; ++x) {
					Vector4 pixel;
					std::memcpy(&pixel, static_cast<uint8_t*>(mapped) + footprint.Offset +
						y * footprint.Footprint.RowPitch + x * sizeof(pixel), sizeof(pixel));
					if (pixel.w > 0.5f) ++result.covered;
					if (x == kSize / 2 && y == kSize / 2) result.center = pixel;
				}
			}
			const D3D12_RANGE empty{};
			readback->Unmap(0, &empty);
			if (!gpu_.CheckMessages()) throw std::runtime_error("Renderer settings GPU diagnostic");
			return result;
		}
		// 実際の描画先ごとの再利用とアウトライン用コピーの独立性を確認する
		bool CheckReflections() {

			return NEMTests::CheckReflectionSurfaceDepth(gpu_.device.Get(), gpu_.queue.GetQueue()) && gpu_.CheckMessages();
		}

		bool CheckAttachments() {

			gpu_.ChangeFrameCount(1);
			MultiRenderTarget targetA, targetB;
			const MultiRenderTargetCreateDesc desc{.width = kSize, .height = kSize, .colors = {{}}};
			targetA.Create(gpu_.device.Get(), &targets_, &depths_, &gpu_.descriptors, desc);
			targetB.Create(gpu_.device.Get(), &targets_, &depths_, &gpu_.descriptors, desc);
			RendererDepthAttachments attachments;
			const auto prepare = [&](const MultiRenderTarget* target, uint32_t domain, uint32_t size = kSize) {
				return attachments.Prepare(gpu_.commands, depths_, gpu_.descriptors, target, domain, size, size);
			};
			if (prepare(nullptr, 0) || prepare(&targetA, 2) || prepare(&targetA, 0, 0)) return false;
			auto* original = prepare(&targetA, 0);
			if (!original) return false;
			ComPtr<ID3D12Resource> originalResource = original->GetResource();
			gpu_.commands.GetCommandList()->ClearDepthStencilView(original->GetDSVCPUHandle(),
				D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0, 37, 0, nullptr);
			if (prepare(&targetA, 0) != original || original->GetResource() != originalResource.Get()) return false;
			auto* otherDomain = prepare(&targetA, 1);
			auto* otherTarget = prepare(&targetB, 0);
			if (!otherDomain || !otherTarget || otherDomain == original || otherTarget == original ||
				otherDomain->GetResource() == otherTarget->GetResource()) return false;
			DepthTextureSnapshot snapshot;
			auto* copy = snapshot.Copy(gpu_.commands, depths_, gpu_.descriptors, *original);
			if (!copy || copy->GetResource() == originalResource.Get() ||
				snapshot.Copy(gpu_.commands, depths_, gpu_.descriptors, *original) != copy) return false;
			copy->Transition(gpu_.commands, D3D12_RESOURCE_STATE_DEPTH_WRITE);
			gpu_.commands.GetCommandList()->ClearDepthStencilView(copy->GetDSVCPUHandle(),
				D3D12_CLEAR_FLAG_STENCIL, 1, 0, 0, nullptr);
			Draw test;
			test.settings.inheritMaterial = false;
			test.settings.cullMode = RendererCullMode::NONE;
			test.settings.depthWrite = false;
			test.settings.depthComparison = RendererDepthComparison::EQUAL;
			test.settings.stencilEnabled = true;
			test.settings.stencilWriteMask = 0;
			test.settings.stencilReference = 37;
			test.settings.frontFace.comparison = RendererDepthComparison::EQUAL;
			test.depth = 0;
			const auto matches = [&](DepthTexture2D* texture) {
				return Render(std::span(&test, 1), 1, 0, texture).center.x == 1;
			};
			if (!matches(original)) return false;
			test.settings.stencilReference = 0;
			if (!matches(copy)) return false;
			test.depth = 1;
			if (!matches(otherDomain) || !matches(otherTarget)) return false;
			// 次フレームでは同じResourceを使い、深度とステンシルを初期化する
			if (prepare(&targetA, 0) != original || original->GetResource() != originalResource.Get() || !matches(original)) return false;
			RendererDepthAttachments swapped;
			attachments.Swap(swapped);
			if (swapped.Prepare(gpu_.commands, depths_, gpu_.descriptors, &targetA, 0, kSize, kSize) != original) return false;
			auto* resized = swapped.Prepare(gpu_.commands, depths_, gpu_.descriptors, &targetA, 0, 16, 16);
			if (!resized || resized->GetWidth() != 16 || resized->GetHeight() != 16 ||
				resized->GetResource() == originalResource.Get()) return false;
			gpu_.Finish();
			snapshot.Destroy();
			swapped.Destroy();
			attachments.Destroy();
			targetA.Destroy();
			targetB.Destroy();
			gpu_.Finish();
			gpu_.ChangeFrameCount(2);
			return gpu_.CheckMessages();
		}
	private:

		NEMTests::ParticleGPUFixture gpu_;
		NEMTests::TestDirectory directory_;
		std::string path_;
		RTVDescriptor targets_;
		DSVDescriptor depths_;
		RenderTexture2D color_;
		DepthTexture2D depth_;
	};

	bool Check(const char* name, bool passed) {

		if (!passed) std::cerr << "Renderer settings GPU: " << name << '\n';
		return passed;
	}
}

bool NEMTests::TestRendererRenderSettingsGPU() {

	try {
		RasterFixture fixture;
		if (!Check("Depth attachments and snapshot", fixture.CheckAttachments())) return false;
		if (!Check("Reflection without depth write", fixture.CheckReflections())) return false;
		Draw draw;
		draw.settings.inheritMaterial = false;
		draw.settings.cullMode = RendererCullMode::NONE;
		draw.settings.depthTest = false;
		const auto render = [&](const Draw& value, float depth = 1.0f, uint8_t stencil = 0) {
			return fixture.Render(std::span(&value, 1), depth, stencil);
		};
		const auto solid = render(draw);
		if (!Check("Solid", solid.center.x == 1 && solid.covered > 100)) return false;
		Draw wire = draw;
		wire.settings.fillMode = RendererFillMode::WireFrame;
		const auto wireResult = render(wire);
		if (!Check("WireFrame", wireResult.center.w == 0 && wireResult.covered > 0 && wireResult.covered < solid.covered)) return false;
		draw.settings.cullMode = RendererCullMode::FRONT;
		const bool frontDrawn = render(draw).center.w == 1;
		draw.settings.cullMode = RendererCullMode::BACK;
		if (!Check("Cull faces", (render(draw).center.w == 1) != frontDrawn)) return false;
		draw.reverse = true;
		if (!Check("Cull winding", (render(draw).center.w == 1) == frontDrawn)) return false;
		draw.reverse = false;
		draw.settings.cullMode = RendererCullMode::NONE;
		draw.settings.depthTest = true;
		// 比較対象を1.0に揃え、D24の量子化に依存せず一致も確認する
		constexpr bool nearPass[]{false, true, false, true, false, true, false, true};
		constexpr bool equalPass[]{false, false, true, true, false, false, true, true};
		constexpr bool farPass[]{false, false, false, false, true, true, true, true};
		for (uint8_t comparison = 1; comparison <= 8; ++comparison) {
			draw.settings.depthComparison = static_cast<RendererDepthComparison>(comparison);
			draw.depth = 0.5f;
			if (!Check("Depth near", (render(draw).center.w == 1) == nearPass[comparison - 1])) return false;
			if (!Check("Depth far", (render(draw, 0.0f).center.w == 1) == farPass[comparison - 1])) return false;
			draw.depth = 1.0f;
			if (!Check("Depth equal", (render(draw).center.w == 1) == equalPass[comparison - 1])) return false;
		}
		draw.settings.depthComparison = RendererDepthComparison::LESS_EQUAL;
		draw.depth = 0.25f;
		Draw behind = draw;
		behind.depth = 0.75f;
		behind.color = Vector4(0, 1, 0, 1);
		for (bool write : {true, false}) {
			draw.settings.depthWrite = write;
			const std::array draws{draw, behind};
			const auto result = fixture.Render(draws);
			if (!Check("Depth write", result.center.x == (write ? 1.0f : 0.0f))) return false;
		}
		draw.settings.depthTest = false;
		draw.settings.stencilEnabled = true;
		draw.settings.stencilReference = 17;
		draw.settings.frontFace.pass = RendererStencilOperation::REPLACE;
		Draw test = draw;
		test.clearColor = true;
		test.color = Vector4(0, 1, 0, 1);
		test.settings.stencilWriteMask = 0;
		test.settings.frontFace.pass = RendererStencilOperation::KEEP;
		test.settings.frontFace.comparison = RendererDepthComparison::EQUAL;
		for (uint8_t reference : std::array<uint8_t, 2>{17, 18}) {
			test.settings.stencilReference = reference;
			const std::array draws{draw, test};
			if (!Check("Stencil reference", (fixture.Render(draws).center.y == 1) == (reference == 17))) return false;
		}
		draw.settings.stencilWriteMask = 15;
		test.settings.stencilReference = 161;
		if (!Check("Stencil write mask", fixture.Render(std::array{draw, test}, 1, 160).center.y == 1)) return false;
		test.settings.stencilReadMask = 15;
		test.settings.stencilReference = 33;
		if (!Check("Stencil read mask", fixture.Render(std::array{draw, test}, 1, 160).center.y == 1)) return false;
		// 比較失敗と深度失敗でも指定したステンシル演算が実行される
		draw.settings.stencilWriteMask = test.settings.stencilReadMask = 255;
		draw.settings.frontFace.comparison = RendererDepthComparison::NEVER;
		draw.settings.frontFace.fail = RendererStencilOperation::ZERO;
		test.settings.stencilReference = 0;
		if (!Check("Stencil fail", fixture.Render(std::array{draw, test}, 1, 17).center.y == 1)) return false;
		draw.settings.frontFace.comparison = RendererDepthComparison::ALWAYS;
		draw.settings.frontFace.fail = RendererStencilOperation::KEEP;
		draw.settings.frontFace.depthFail = RendererStencilOperation::REPLACE;
		draw.settings.depthTest = true;
		draw.settings.depthComparison = RendererDepthComparison::NEVER;
		test.settings.stencilReference = 17;
		if (!Check("Stencil depth fail", fixture.Render(std::array{draw, test}).center.y == 1)) return false;
		draw.settings.depthTest = false;
		draw.settings.frontFace.depthFail = RendererStencilOperation::KEEP;
		draw.settings.separateBackFace = true;
		draw.settings.backFace.pass = RendererStencilOperation::ZERO;
		const bool frontStencil = fixture.Render(std::array{draw, test}).center.y == 1;
		draw.reverse = true;
		if (!Check("Stencil separate faces", (fixture.Render(std::array{draw, test}).center.y == 1) != frontStencil)) return false;
		draw.reverse = false;
		// 無効化した深度テストがNEVERを引き継がないことを確認する
		draw.settings.stencilEnabled = false;
		draw.settings.depthComparison = RendererDepthComparison::NEVER;
		if (!Check("Depth disabled", render(draw).center.x == 1)) return false;
		PipelineCacheKey first, second;
		first.rendererSettings = NormalizeRendererPipelineSettings(&test.settings);
		test.settings.stencilReference = 200;
		second.rendererSettings = NormalizeRendererPipelineSettings(&test.settings);
		if (!Check("Reference cache key", first == second)) return false;
		test.settings.fillMode = RendererFillMode::WireFrame;
		second.rendererSettings = NormalizeRendererPipelineSettings(&test.settings);
		if (!Check("State cache key", !(first == second))) return false;
		test.settings.inheritMaterial = true;
		if (!Check("Inherited cache key", NormalizeRendererPipelineSettings(&test.settings) ==
			NormalizeRendererPipelineSettings(nullptr))) return false;
		Draw inherited;
		inherited.settings = test.settings;
		if (!Check("Inherited raster", render(inherited).covered == solid.covered)) return false;
		return true;
	} catch (const std::exception& error) {
		std::cerr << "Renderer settings GPU: " << error.what() << '\n';
		return false;
	}
}
