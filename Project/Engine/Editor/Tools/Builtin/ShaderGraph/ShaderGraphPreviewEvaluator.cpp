#include "ShaderGraphPreviewEvaluator.h"

//============================================================================
//	include
//============================================================================
#include "ShaderGraphNodePreviewUtility.h"
#include "ShaderGraphPreviewDependencies.h"
#include "ShaderGraphPreviewFingerprint.h"
#include "ShaderGraphPreviewPipeline.h"
#include <Engine/Core/Rendering/Core/GraphicsFrameResourceArray.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Views/ViewportRenderService.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphFrameConstants.h>

// c++
#include <algorithm>
#include <array>
#include <format>
#include <unordered_map>
#include <unordered_set>

using namespace Engine;
using namespace Engine::ShaderGraphNodePreviewUtility;

namespace {

	constexpr uint32_t kAtlasColumns = 8;
	constexpr uint32_t kAtlasCapacity = kAtlasColumns * kAtlasColumns;
	constexpr uint32_t kTilePadding = 2;

	struct PreviewEntry {

		Engine::UUID nodeID{};
		ShaderGraphNodePreview view{};
		ShaderGraphNodePreviewCompileOutput compiled{};
		std::string pipelineKey;
		std::shared_ptr<ShaderGraphPreviewPipeline> pipeline;
		uint32_t slot = 0;
		bool attempted = false;
		bool dirty = true;
	};

	// ノードごとの診断を表示用にまとめる
	std::string DiagnosticText(const ShaderGraphNodePreviewCompileOutput& compiled) {

		std::string text;
		for (const ShaderGraphDiagnostic& diagnostic : compiled.diagnostics) {
			if (!text.empty()) text += '\n';
			text += diagnostic.message;
		}
		return text;
	}

	// 描画済みSceneの入力が揃っているときだけ実画像を参照する
	std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 7> ResolveSceneInputs(const EditorPanelContext& context) {

		std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 7> handles{};
		if (!context.renderPipeline || !context.viewportRenderService) return handles;
		if (const auto* color = context.viewportRenderService->GetDisplayTexture(RenderViewKind::Scene)) {
			handles[0] = color->GetSRVGPUHandle();
		}
		if (const auto* depth = context.renderPipeline->GetViewDepthTexture(RenderViewKind::Scene)) {
			handles[1] = depth->GetSRVGPUHandle();
		}
		constexpr GBufferAttachment attachments[]{GBufferAttachment::Normal, GBufferAttachment::Position,
			GBufferAttachment::Material, GBufferAttachment::Emissive, GBufferAttachment::Flags};
		for (uint32_t index = 0; index < std::size(attachments); ++index) {
			if (const auto* texture = context.renderPipeline->GetViewGBufferTexture(RenderViewKind::Scene, attachments[index])) {
				handles[index + 2] = texture->GetSRVGPUHandle();
			}
		}
		return handles;
	}
}

struct ShaderGraphPreviewEvaluator::PreviewState {

	std::vector<PreviewEntry> entries;
	std::unordered_map<std::string, std::shared_ptr<ShaderGraphPreviewPipeline>> pipelines;
	std::unordered_set<std::string> pages;
	std::vector<std::pair<std::string, uint64_t>> retiredPages;
	FrameConstantBufferAllocator constants;
	GraphicsFrameResourceArray<std::vector<TextureOwnership>> textureOwners;
	std::array<uint64_t, kGraphicsFrameContextCount> ownerSerials{};
	uint64_t graphHash = 0;
	uint64_t dependencyHash = 0;
	uint64_t textureRevision = 0;
	float lastDependencyCheck = -1.0f;
	int32_t textureSize = 0;
	bool valid = false;
};

ShaderGraphPreviewEvaluator::ShaderGraphPreviewEvaluator() = default;

ShaderGraphPreviewEvaluator::~ShaderGraphPreviewEvaluator() {

	ClearNodePreviews();
}

void ShaderGraphPreviewEvaluator::Init() {

	previewState_ = std::make_unique<PreviewState>();
}

const ShaderGraphNodePreview* ShaderGraphPreviewEvaluator::FindPreview(Engine::UUID nodeID) const {

	if (!previewState_) return nullptr;
	const auto& entries = previewState_->entries;
	const auto found = std::find_if(entries.begin(), entries.end(),
		[&](const PreviewEntry& entry) { return entry.nodeID == nodeID; });
	return found != entries.end() ? &found->view : nullptr;
}

void ShaderGraphPreviewEvaluator::Update(const EditorToolContext& context, const ShaderGraphAsset& graph,
	int32_t textureSize, float time, float deltaTime, std::string& status) {

	if (!previewState_) Init();
	resources_.BeginEditorToolFrame(context);
	UpdateResources(context, graph, textureSize, time, deltaTime, status);
	resources_.EndEditorToolFrame();
}

void ShaderGraphPreviewEvaluator::UpdateResources(const EditorToolContext& context, const ShaderGraphAsset& graph,
	int32_t textureSize, float time, float deltaTime, std::string& status) {

	if (!previewState_ || !context.panelContext || !context.panelContext->graphicsCore) return;
	PreviewState& state = *previewState_;
	GraphicsCore& graphics = *context.panelContext->graphicsCore;
	auto& platform = graphics.GetDXObject();
	auto& retirement = platform.GetResourceRetirement();
	AssetDatabase* database = context.panelContext->editorContext ? context.panelContext->editorContext->assetDatabase : nullptr;
	textureSize = std::clamp(textureSize, 32, 512);
	const uint64_t graphHash = CalculatePreviewHash(graph);
	uint64_t dependencyHash = state.dependencyHash;
	if (!state.valid || state.graphHash != graphHash || time - state.lastDependencyCheck >= 0.25f) {
		dependencyHash = ShaderGraphPreviewDependencies::CalculateHash(graph, database);
		state.lastDependencyCheck = time;
	}
	const bool changed = !state.valid || state.graphHash != graphHash || state.dependencyHash != dependencyHash ||
		state.textureSize != textureSize;
	if (changed) {
		state.entries.clear();
		const ShaderGraphAsset resolved = ShaderGraphPreviewDependencies::ResolveFunctions(graph, database);
		const ShaderGraphAssetResolver resolver = [database](AssetID id, ShaderGraphAsset& child) {
			return ShaderGraphPreviewDependencies::LoadGraph(id, database, child);
		};
		for (const ShaderGraphNode& node : resolved.nodes) {
			if (!IsPreviewableNode(node) || !node.previewExpanded) continue;
			PreviewEntry entry{};
			entry.nodeID = node.id;
			entry.slot = static_cast<uint32_t>(state.entries.size());
			ShaderGraphNodePreviewOptions options{};
			options.sphere = node.previewMode == ShaderGraphPreviewMode::ThreeDimensional;
			entry.compiled = ShaderGraphNodePreviewCompiler::Compile(resolved, node.id, node.previewOutputSlot, options, resolver);
			if (node.previewMode == ShaderGraphPreviewMode::Inherit && entry.compiled.usesGeometry) {
				options.sphere = true;
				entry.compiled = ShaderGraphNodePreviewCompiler::Compile(resolved, node.id, node.previewOutputSlot, options, resolver);
			}
			entry.view.diagnostic = DiagnosticText(entry.compiled);
			if (entry.compiled.Succeeded()) {
				entry.compiled.pixelHLSL += std::format("\n// dependency {:016x}\n", dependencyHash);
				entry.pipelineKey = BuildShaderGraphNodePreviewPipelineKey(entry.compiled);
				if (const auto cached = state.pipelines.find(entry.pipelineKey); cached != state.pipelines.end()) {
					entry.pipeline = cached->second;
					entry.attempted = true;
				}
			} else {
				entry.attempted = true;
			}
			state.entries.emplace_back(std::move(entry));
		}
		state.graphHash = graphHash;
		state.dependencyHash = dependencyHash;
		state.textureSize = textureSize;
		state.valid = true;
	}

	// 初回コンパイルを分散して編集中の入力を妨げない
	for (PreviewEntry& entry : state.entries) {
		if (entry.attempted) continue;
		entry.attempted = true;
		entry.pipeline = ShaderGraphPreviewPipelineBuilder::Create(graphics, entry.compiled, entry.view.diagnostic);
		if (entry.pipeline) state.pipelines[entry.pipelineKey] = entry.pipeline;
		break;
	}
	if (state.pipelines.size() > 256) {
		std::erase_if(state.pipelines, [](const auto& pair) { return pair.second.use_count() == 1; });
	}

	const uint64_t serial = GraphicsFrameState::GetFrameSerial();
	const uint32_t frame = GraphicsFrameState::GetCurrentIndex();
	state.textureOwners.Connect(retirement);
	if (state.ownerSerials[frame] != serial) {
		state.textureOwners[frame].clear();
		state.ownerSerials[frame] = serial;
	}
	state.constants.BeginFrame();
	const uint64_t revision = RuntimeTextureResolver::GetBindingRevision();
	const bool texturesChanged = state.textureRevision != revision;
	state.textureRevision = revision;
	const uint32_t pitch = static_cast<uint32_t>(textureSize) + kTilePadding * 2;
	const uint32_t atlasSize = pitch * kAtlasColumns;
	const uint32_t pageCount = (static_cast<uint32_t>(state.entries.size()) + kAtlasCapacity - 1) / kAtlasCapacity;
	const auto sceneHandles = ResolveSceneInputs(*context.panelContext);
	std::unordered_set<std::string> pages;
	for (uint32_t page = 0; page < pageCount; ++page) {
		const std::string name = std::format("ShaderGraphPreviewAtlas_{}_{}", textureSize, page);
		pages.insert(name);
		EditorToolRenderTexture* atlas = resources_.CreateRenderTexture(name,
			Vector2I(atlasSize, atlasSize), Color4(0.0f, 0.0f, 0.0f, 0.0f), 1, false, DXGI_FORMAT_R16G16B16A16_FLOAT);
		if (!atlas) {
			status = "ノードプレビューの描画先を作成できません";
			continue;
		}
		const bool needsUpdate = std::any_of(state.entries.begin(), state.entries.end(), [&](const PreviewEntry& entry) {
			return entry.slot / kAtlasCapacity == page && entry.pipeline &&
				(entry.dirty || texturesChanged || entry.compiled.usesTime || entry.compiled.usesScene);
		});
		if (!needsUpdate) continue;
		resources_.RenderToTexture(*atlas, [&](const EditorToolRenderContext& renderContext) {
			auto* command = renderContext.dxCommand->GetCommandList();
			for (PreviewEntry& entry : state.entries) {
				if (entry.slot / kAtlasCapacity != page || !entry.pipeline) continue;
				if (!entry.dirty && !texturesChanged && !entry.compiled.usesTime && !entry.compiled.usesScene) continue;
				PipelineState& pipeline = *entry.pipeline->pipeline;
				bool sceneAvailable = true;
				for (uint32_t index = 0; index < sceneHandles.size(); ++index) {
					if (pipeline.FindBinding(ShaderBindingKind::SRV, index, 4) && !sceneHandles[index].ptr) sceneAvailable = false;
				}
				if (!sceneAvailable) {
					entry.view.textureID = 0;
					entry.view.diagnostic = "SceneViewを描画するとプレビューできます";
					continue;
				}
				bool retry = false;
				const auto resolver = [&](MaterialParameterSemantic semantic, const AssetID& id) {
					const TextureColorSpace colorSpace = semantic == MaterialParameterSemantic::BaseColorTexture ||
						semantic == MaterialParameterSemantic::EmissiveTexture ? TextureColorSpace::SRGB : TextureColorSpace::Auto;
					auto texture = RuntimeTextureResolver::ResolveBindless(graphics, database, id, colorSpace);
					retry |= texture.retry;
					state.textureOwners[frame].emplace_back(texture.ownership);
					return MaterialParameterBufferBuilder::TextureResolveResult{texture.srvIndex, !texture.retry, texture.ownership};
				};
				const std::vector<uint8_t> bytes = MaterialParameterBufferBuilder::BuildElement(
					entry.compiled.defaultParameters, {}, entry.pipeline->layout, resolver);
				command->SetPipelineState(pipeline.GetGraphicsPipeline(BlendMode::Normal));
				command->SetGraphicsRootSignature(pipeline.GetRootSignature());
				pipeline.BindGlobalDescriptorTablesGraphics(command, graphics.GetSRVDescriptor().GetGPUHandle(0));
				const auto bindConstant = [&](uint32_t slot, const FrameConstantBufferAllocation& allocation) {
					if (const auto* binding = pipeline.FindBinding(ShaderBindingKind::CBV, slot, 0)) {
						RootBindingCommand::SetGraphicsCBV(command, binding, allocation.gpuAddress);
					}
				};
				if (!bytes.empty()) bindConstant(0, state.constants.AllocateAndUploadBytes(retirement, platform.GetDevice(), bytes));
				const Vector4 settings(0.0f, 0.0f, static_cast<float>(textureSize), 0.0f);
				bindConstant(1, state.constants.AllocateAndUpload(retirement, platform.GetDevice(), settings));
				ShaderGraphTimeConstantsGPU times{};
				times.time = times.unscaledTime = time;
				times.deltaTime = times.smoothDeltaTime = deltaTime;
				times.cameraPosition = {0.0f, 0.0f, 3.0f, 1.0f};
				times.cameraDirection = {0.0f, 0.0f, -1.0f, 0.0f};
				times.viewToWorld = Matrix4x4::MakeRotateMatrix({0.0f, 180.0f, 0.0f}) *
					Matrix4x4::MakeTranslateMatrix({0.0f, 0.0f, 3.0f});
				times.worldToView = Matrix4x4::Inverse(times.viewToWorld);
				times.viewToClip = Matrix4x4::MakePerspectiveFovMatrix(60.0f, 1.0f, 0.1f, 1000.0f);
				times.clipToView = Matrix4x4::Inverse(times.viewToClip);
				times.worldToClip = times.worldToView * times.viewToClip;
				times.clipToWorld = times.clipToView * times.viewToWorld;
				times.screenSize = {static_cast<float>(textureSize), static_cast<float>(textureSize), 2.0f, 2.0f};
				times.viewport = {0.0f, 0.0f, static_cast<float>(textureSize), static_cast<float>(textureSize)};
				bindConstant(4, state.constants.AllocateAndUpload(retirement, platform.GetDevice(), times));
				for (uint32_t index = 0; index < sceneHandles.size(); ++index) {
					if (const auto* binding = pipeline.FindBinding(ShaderBindingKind::SRV, index, 4)) {
						RootBindingCommand::SetGraphicsSRV(command, binding, 0, sceneHandles[index]);
					}
				}
				const uint32_t column = entry.slot % kAtlasColumns;
				const uint32_t row = (entry.slot % kAtlasCapacity) / kAtlasColumns;
				const LONG x = static_cast<LONG>(column * pitch);
				const LONG y = static_cast<LONG>(row * pitch);
				const D3D12_RECT clearRect{x, y, x + static_cast<LONG>(pitch), y + static_cast<LONG>(pitch)};
				const float clear[4]{};
				command->ClearRenderTargetView(atlas->GetColorTexture()->GetRenderTarget().rtvHandle, clear, 1, &clearRect);
				const D3D12_VIEWPORT viewport{static_cast<float>(x + kTilePadding), static_cast<float>(y + kTilePadding),
					static_cast<float>(textureSize), static_cast<float>(textureSize), 0.0f, 1.0f};
				const D3D12_RECT scissor{x + static_cast<LONG>(kTilePadding), y + static_cast<LONG>(kTilePadding),
					x + static_cast<LONG>(kTilePadding) + textureSize, y + static_cast<LONG>(kTilePadding) + textureSize};
				command->RSSetViewports(1, &viewport);
				command->RSSetScissorRects(1, &scissor);
				command->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				command->DrawInstanced(3, 1, 0, 0);
				entry.view.textureID = atlas->GetImTextureID();
				entry.view.uvMinimum = Vector2(static_cast<float>(scissor.left) / atlasSize, static_cast<float>(scissor.top) / atlasSize);
				entry.view.uvMaximum = Vector2(static_cast<float>(scissor.right) / atlasSize, static_cast<float>(scissor.bottom) / atlasSize);
				entry.view.diagnostic.clear();
				entry.dirty = retry;
			}
		}, Color4(0.0f, 0.0f, 0.0f, 0.0f), false);
	}
	for (const auto& previous : state.pages) {
		if (!pages.contains(previous)) state.retiredPages.emplace_back(previous, serial);
	}
	state.pages = std::move(pages);
	std::erase_if(state.retiredPages, [&](const auto& retired) {
		if (state.pages.contains(retired.first)) return true;
		if (serial <= retired.second + kGraphicsFrameContextCount) return false;
		resources_.DestroyRenderTexture(retired.first);
		return true;
	});
}

void ShaderGraphPreviewEvaluator::InvalidateNodePreviews() {

	if (previewState_) previewState_->valid = false;
}

void ShaderGraphPreviewEvaluator::ClearNodePreviews() {

	previewState_.reset();
	resources_.ClearRenderTextures();
}
