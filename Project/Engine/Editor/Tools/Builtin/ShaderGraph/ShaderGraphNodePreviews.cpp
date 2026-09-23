#include "ShaderGraphNodePreviews.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineStateBuilder.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxConstantBuffer.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Textures/BuiltinTextureLibrary.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include "ShaderGraphNodePreviewUtility.h"

// c++
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <span>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <imgui_node_editor.h>


using namespace Engine::ShaderGraphNodePreviewUtility;

namespace {
	constexpr uint32_t kNodePreviewRTVReserve = 16;
}

struct Engine::ShaderGraphNodePreviews::PreviewState {

	PreviewState() {

		for (uint32_t index = 0;
			index < inputSlots.size(); ++index) {

			inputSlots[index] =
				bindCache.AddSlotByRegister(
					ShaderBindingKind::SRV,
					index, 0);
		}
		constantsSlot =
			bindCache.AddSlotByRegister(
				ShaderBindingKind::CBV, 0, 0);
	}

	~PreviewState();

	GraphicsResourceRetirement* retirement = nullptr;
	std::unique_ptr<PipelineState> pipeline{};
	PipelineBindingCache bindCache{};
	bool pipelineInitialized = false;
	bool pipelineAttempted = false;

	std::array<PipelineBindingCache::SlotID, 4>
		inputSlots{};
	PipelineBindingCache::SlotID constantsSlot =
		PipelineBindingCache::kInvalidSlot;

	std::array<std::vector<
		std::unique_ptr<DxConstBuffer<PreviewConstants>>>,
		kGraphicsFrameContextCount> constantBuffers{};
	std::array<uint32_t, kGraphicsFrameContextCount>
		constantBufferIndices{};
	std::array<uint64_t, kGraphicsFrameContextCount>
		constantBufferFrameSerials{};

	std::unordered_set<std::string> textureNames{};
	std::vector<std::pair<std::string, uint64_t>>
		retiredTextures{};
	std::unordered_map<uint64_t, uint32_t>
		textureIndices{};
	uint64_t graphHash = 0;
	bool previewsValid = false;
	bool descriptorLimitReached = false;

	DxConstBuffer<PreviewConstants>& AllocateConstantBuffer(
		GraphicsCore& graphicsCore) {

		const uint32_t frameIndex =
			GraphicsFrameState::GetCurrentIndex();
		const uint64_t frameSerial =
			GraphicsFrameState::GetFrameSerial();
		if (constantBufferFrameSerials[frameIndex] !=
			frameSerial) {

			constantBufferFrameSerials[frameIndex] =
				frameSerial;
			constantBufferIndices[frameIndex] = 0;
		}

		auto& buffers = constantBuffers[frameIndex];
		uint32_t& index =
			constantBufferIndices[frameIndex];
		if (buffers.size() <= index) {

			auto buffer = std::make_unique<
				DxConstBuffer<PreviewConstants>>();
			buffer->CreateBuffer(
				graphicsCore.GetDXObject().GetDevice());
			buffers.emplace_back(std::move(buffer));
		}
		return *buffers[index++];
	}
};

Engine::ShaderGraphNodePreviews::PreviewState::~PreviewState() {

	if (!retirement) {
		return;
	}
	for (const auto& buffers : constantBuffers) {
		for (const auto& buffer : buffers) {
			retirement->Retire(ComPtr<ID3D12Resource>(buffer->GetResource()));
		}
	}
	if (pipeline) {
		pipeline->RetireGPUObjects(*retirement);
	}
}

Engine::ShaderGraphNodePreviews::ShaderGraphNodePreviews() = default;

Engine::ShaderGraphNodePreviews::~ShaderGraphNodePreviews() {

	ClearNodePreviews();
}

void Engine::ShaderGraphNodePreviews::Init() {

	previewState_ = std::make_unique<PreviewState>();
}

void Engine::ShaderGraphNodePreviews::UpdateNodePreviews(const EditorToolContext& context, const ShaderGraphAsset& graph,
	const ShaderGraphAppearanceSetting& appearance, std::string& status) {

	resources_.BeginEditorToolFrame(context);
	UpdateResources(context, graph, appearance, status);
	resources_.EndEditorToolFrame();
}

void Engine::ShaderGraphNodePreviews::UpdateResources(const EditorToolContext& context, const ShaderGraphAsset& graph,
	const ShaderGraphAppearanceSetting& appearance, std::string& status) {

	if (!previewState_ ||
		!context.panelContext ||
		!context.panelContext->graphicsCore) {

		return;
	}

	GraphicsCore& graphicsCore =
		*context.panelContext->graphicsCore;
	PreviewState& state = *previewState_;
	state.retirement = &graphicsCore.GetDXObject().GetResourceRetirement();
	if (!state.pipelineAttempted) {

		state.pipelineAttempted = true;

		GraphicsPipelineDesc desc{};
		desc.type = PipelineType::Vertex;
		desc.preRaster.file =
			"Builtin/FullscreenCopy/fullscreenCopy.VS.hlsl";
		desc.preRaster.entry = "main";
		desc.preRaster.profile = "vs_6_0";
		desc.pixel.file =
			"Builtin/ShaderGraphPreview/"
			"shaderGraphPreview.PS.hlsl";
		desc.pixel.entry = "main";
		desc.pixel.profile = "ps_6_6";

		D3D12_STATIC_SAMPLER_DESC sampler{};
		sampler.Filter =
			D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		sampler.AddressU =
			D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressV =
			D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.AddressW =
			D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		sampler.ComparisonFunc =
			D3D12_COMPARISON_FUNC_ALWAYS;
		sampler.MaxLOD = D3D12_FLOAT32_MAX;
		sampler.ShaderRegister = 0;
		sampler.RegisterSpace = 0;
		sampler.ShaderVisibility =
			D3D12_SHADER_VISIBILITY_PIXEL;
		desc.staticSamplers.emplace_back(sampler);

		desc.rasterizer =
			CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		desc.rasterizer.CullMode =
			D3D12_CULL_MODE_NONE;
		desc.depthStencil =
			CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		desc.depthStencil.DepthEnable = FALSE;
		desc.depthStencil.DepthWriteMask =
			D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.depthStencil.StencilEnable = FALSE;
		desc.sampleDesc = { 1, 0 };
		desc.topologyType =
			D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.numRenderTargets = 1;
		desc.rtvFormats[0] =
			DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.dsvFormat =
			DXGI_FORMAT_D24_UNORM_S8_UINT;

		state.pipelineInitialized =
			(state.pipeline = PipelineStateBuilder::CreateGraphics(
				graphicsCore.GetDXObject().GetDevice(),
				graphicsCore.GetDXObject().
				GetDxShaderCompiler(),
				desc)) != nullptr;
	}
	if (!state.pipelineInitialized) {
		return;
	}

	// 展開中プレビューとその依存ノードに必要なRTだけを維持する
	const std::vector<const ShaderGraphNode*> previewOrder =
		BuildPreviewOrder(graph);
	std::unordered_set<std::string> textureNames{};
	for (const ShaderGraphNode* node : previewOrder) {
		if (!node) {
			continue;
		}

		const std::string name =
			PreviewTextureName(node->id);
		textureNames.insert(name);
	}
	std::erase_if(
		state.retiredTextures,
		[&](const auto& retired) {
			return textureNames.contains(retired.first);
		});
	for (const std::string& name :
		state.textureNames) {

		if (!textureNames.contains(name)) {
			const auto found = std::find_if(
				state.retiredTextures.begin(),
				state.retiredTextures.end(),
				[&](const auto& retired) {
					return retired.first == name;
				});
			if (found == state.retiredTextures.end()) {
				state.retiredTextures.emplace_back(
					name,
					GraphicsFrameState::GetFrameSerial());
			}
		}
	}
	const uint64_t frameSerial =
		GraphicsFrameState::GetFrameSerial();
	std::erase_if(
		state.retiredTextures,
		[&](const auto& retired) {
			if (frameSerial < retired.second +
				kGraphicsFrameContextCount + 1u) {

				return false;
			}
			resources_.DestroyRenderTexture(retired.first);
			return true;
		});
	bool descriptorLimitReached = false;
	for (const std::string& name : textureNames) {
		if (!resources_.FindRenderTexture(name)) {
			const RTVDescriptor& rtvDescriptor =
				graphicsCore.GetRTVDescriptor();
			if (rtvDescriptor.GetMaxDescriptorCount() <=
				rtvDescriptor.GetUseDescriptorCount() +
				kNodePreviewRTVReserve ||
				!resources_.CreateRenderTexture(
					name,
					Vector2I(
						appearance.
						nodePreviewTextureSize,
						appearance.
						nodePreviewTextureSize),
					Color4::Black(), 1, false)) {

				descriptorLimitReached = true;
			}
		}
	}
	if (descriptorLimitReached &&
		!state.descriptorLimitReached) {

		status =
			"ノードプレビュー用RTVの空きがありません";
	}
	state.descriptorLimitReached =
		descriptorLimitReached;
	state.textureNames = std::move(textureNames);

	std::unordered_map<uint64_t, uint32_t>
		textureIndices{};
	for (const ShaderGraphNode* node : previewOrder) {
		if (!node) {
			continue;
		}

		uint32_t textureIndex = UINT32_MAX;
		const PreviewTextureReference reference =
			ResolvePreviewTextureReference(
				graph, *node);
		if (reference.assetID) {
			const GPUTextureResource* texture =
				RuntimeTextureResolver::Resolve(
					graphicsCore,
					context.toolContext.assetDatabase,
					reference.assetID,
					reference.sRGB ?
						TextureColorSpace::SRGB :
						TextureColorSpace::Linear);
			if (texture && texture->valid) {
				textureIndex = texture->srvIndex;
			}
		}
		textureIndices[node->id.value] =
			textureIndex;
	}

	const uint64_t graphHash =
		CalculatePreviewHash(graph);
	const bool textureChanged =
		textureIndices != state.textureIndices;
	const bool timeDependent =
		std::any_of(
			previewOrder.begin(), previewOrder.end(),
			[](const ShaderGraphNode* node) {
				return node &&
					node->kind == ShaderGraphNodeKind::Time;
			});
	if (state.previewsValid &&
		state.graphHash == graphHash &&
		!textureChanged && !timeDependent) {

		return;
	}

	state.textureIndices =
		std::move(textureIndices);
	state.graphHash = graphHash;
	state.previewsValid = false;

	const GPUTextureResource* whiteTexture =
		graphicsCore.GetBuiltinTextureLibrary().
		GetWhiteTexture();
	const D3D12_GPU_DESCRIPTOR_HANDLE fallbackHandle =
		whiteTexture && whiteTexture->valid ?
		whiteTexture->gpuHandle :
		D3D12_GPU_DESCRIPTOR_HANDLE{};

	for (const ShaderGraphNode* node : previewOrder) {

		if (!node) {
			continue;
		}
		EditorToolRenderTexture* destination =
			resources_.FindRenderTexture(
				PreviewTextureName(node->id));
		if (!destination ||
			!destination->IsValid()) {

			continue;
		}

		PreviewConstants constants{};
		const float previewTime =
			static_cast<float>(ImGui::GetTime());
		const float previewDeltaTime =
			ImGui::GetIO().DeltaTime;
		constants.timeValues = Vector4(
			previewTime,
			std::sin(previewTime),
			std::cos(previewTime),
			previewDeltaTime);
		constants.operation =
			static_cast<uint32_t>(
				GetPreviewOperation(graph, *node));
		constants.outputValueType =
			static_cast<uint32_t>(
				ResolvePreviewOutputType(
					graph, *node));
		const auto textureIndex =
			state.textureIndices.find(
				node->id.value);
		if (textureIndex !=
			state.textureIndices.end()) {

			constants.textureIndex =
				textureIndex->second;
		}

		if (node->kind ==
			ShaderGraphNodeKind::Parameter) {

			const ShaderGraphParameter* parameter =
				FindPreviewParameter(
					graph, node->parameterID);
			if (parameter) {
				constants.literalValue =
					ToPreviewVector(
						parameter->defaultValue,
						parameter->type);
				if (parameter->type ==
					ShaderGraphValueType::Texture2D) {

					constants.literalValue =
						Vector4(
							1.0f, 1.0f,
							1.0f, 1.0f);
				}
			}
		} else {
			constants.literalValue =
				ToPreviewVector(
					node->value,
					node->kind ==
					ShaderGraphNodeKind::TextureSample ?
					ShaderGraphValueType::Color :
					node->valueType);
		}

		std::array<D3D12_GPU_DESCRIPTOR_HANDLE, 4>
			inputHandles{
				fallbackHandle, fallbackHandle,
				fallbackHandle, fallbackHandle,
		};
		for (uint32_t inputSlot = 0;
			inputSlot < inputHandles.size();
			++inputSlot) {

			constants.inputDefaults[inputSlot] =
				PreviewInputDefault(
					node->kind, inputSlot);
			const ShaderGraphLink* link =
				FindPreviewInput(
					graph, *node, inputSlot);
			if (!link) {
				continue;
			}

			const ShaderGraphNode* source =
				FindPreviewNode(
					graph, link->outputNode);
			if (source && source->kind ==
				ShaderGraphNodeKind::Time) {

				const std::array timeOutputs{
					previewTime,
					std::sin(previewTime),
					std::cos(previewTime),
					previewDeltaTime,
					previewDeltaTime,
				};
				if (link->outputSlot < timeOutputs.size()) {
					const float value =
						timeOutputs[link->outputSlot];
					constants.inputDefaults[inputSlot] =
						Vector4(value, value, value, value);
				}
				continue;
			}
			const EditorToolRenderTexture*
				sourceTexture = source ?
				resources_.FindRenderTexture(
					PreviewTextureName(
						source->id)) :
				nullptr;
			const RenderTexture2D* sourceValue =
				sourceTexture ?
				sourceTexture->GetColorTexture(0) :
				nullptr;
			if (!source || !sourceValue) {
				continue;
			}

			inputHandles[inputSlot] =
				sourceValue->GetSRVGPUHandle();
			constants.connectedMask |=
				1u << inputSlot;
			constants.inputSwizzles[inputSlot] =
				static_cast<uint32_t>(
					ResolvePreviewSwizzle(
						graph, *source,
						link->outputSlot));
		}

		resources_.RenderToTexture(
			*destination,
			[&](const EditorToolRenderContext&
				renderContext) {

					ID3D12GraphicsCommandList*
						commandList =
						renderContext.dxCommand->
						GetCommandList();
					commandList->SetGraphicsRootSignature(
						state.pipeline->GetRootSignature());
					commandList->SetPipelineState(
						state.pipeline->GetGraphicsPipeline(
							BlendMode::Normal));

					state.bindCache.Sync(*state.pipeline);
					for (uint32_t inputSlot = 0;
						inputSlot <
						inputHandles.size();
						++inputSlot) {

						if (state.bindCache.Has(
							state.inputSlots[inputSlot])) {

							RootBindingCommand::
								SetGraphicsSRV(
									commandList,
									state.bindCache.Get(
										state.inputSlots[
											inputSlot]),
											0,
											inputHandles[
												inputSlot]);
						}
					}

					DxConstBuffer<PreviewConstants>&
						constantBuffer =
						state.AllocateConstantBuffer(
							graphicsCore);
					constantBuffer.TransferData(constants);
					if (state.bindCache.Has(
						state.constantsSlot)) {

						RootBindingCommand::
							SetGraphicsCBV(
								commandList,
								state.bindCache.Get(
									state.constantsSlot),
								constantBuffer.GetResource()->
								GetGPUVirtualAddress());
					}

					commandList->IASetPrimitiveTopology(
						D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					commandList->DrawInstanced(
						3, 1, 0, 0);
			});
	}
	state.previewsValid = true;
}

void Engine::ShaderGraphNodePreviews::DrawNodePreview(
	ShaderGraphNode& node,
	float nodeWidth, const ShaderGraphAppearanceSetting& appearance) {

	if (!IsPreviewableNode(node.kind) ||
		!node.previewExpanded) {

		return;
	}

	const EditorToolRenderTexture* texture =
		resources_.FindRenderTexture(
			PreviewTextureName(node.id));
	const ImTextureID textureID =
		texture ?
		texture->GetImTextureID(0) :
		static_cast<ImTextureID>(0);
	const float displaySize =
		(std::min)(
			nodeWidth,
			appearance.
			nodePreviewDisplaySize);
	const float offsetX =
		(nodeWidth - displaySize) * 0.5f;
	const ImVec2 rowMinimum =
		ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos(
		ImVec2(
			rowMinimum.x + offsetX,
			rowMinimum.y));

	const ImVec2 imageMinimum =
		ImGui::GetCursorScreenPos();
	const ImVec2 imageMaximum{
		imageMinimum.x + displaySize,
		imageMinimum.y + displaySize,
	};
	ImDrawList* drawList =
		ImGui::GetWindowDrawList();
	constexpr float checkerSize = 8.0f;
	const ImU32 checkerColors[2]{
		IM_COL32(58, 58, 58, 255),
		IM_COL32(92, 92, 92, 255),
	};
	for (float y = imageMinimum.y;
		y < imageMaximum.y;
		y += checkerSize) {

		for (float x = imageMinimum.x;
			x < imageMaximum.x;
			x += checkerSize) {

			const int32_t column =
				static_cast<int32_t>(
					(x - imageMinimum.x) /
					checkerSize);
			const int32_t row =
				static_cast<int32_t>(
					(y - imageMinimum.y) /
					checkerSize);
			drawList->AddRectFilled(
				ImVec2(x, y),
				ImVec2(
					(std::min)(
						x + checkerSize,
						imageMaximum.x),
					(std::min)(
						y + checkerSize,
						imageMaximum.y)),
				checkerColors[
					(column + row) & 1]);
		}
	}

	if (textureID) {
		ImGui::Image(
			textureID,
			ImVec2(displaySize, displaySize));
	} else {
		ImGui::Dummy(
			ImVec2(displaySize, displaySize));
	}
	drawList->AddRect(
		imageMinimum, imageMaximum,
		ImGui::GetColorU32(
			ImGuiCol_Border));
	ImGui::SetCursorScreenPos(
		ImVec2(
			rowMinimum.x,
			imageMaximum.y));
	ImGui::Dummy(ImVec2(nodeWidth, 1.0f));
}

void Engine::ShaderGraphNodePreviews::InvalidateNodePreviews() {

	if (!previewState_) {
		return;
	}
	previewState_->graphHash = 0;
	previewState_->previewsValid = false;
}

void Engine::ShaderGraphNodePreviews::ClearNodePreviews() {

	if (!previewState_) {
		return;
	}

	std::unordered_set<std::string> textureNames =
		previewState_->textureNames;
	for (const auto& retired :
		previewState_->retiredTextures) {

		textureNames.insert(retired.first);
	}
	for (const std::string& name : textureNames) {

		resources_.DestroyRenderTexture(name);
	}
	previewState_->textureNames.clear();
	previewState_->retiredTextures.clear();
	previewState_->textureIndices.clear();
	previewState_->graphHash = 0;
	previewState_->previewsValid = false;
	previewState_->descriptorLimitReached = false;
}
