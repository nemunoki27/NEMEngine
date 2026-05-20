#include "PostProcessExecutor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Common/D3D12Utils.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

// c++
#include <algorithm>

//============================================================================
//	PostProcessExecutor classMethods
//============================================================================

namespace {

	constexpr const char* kFrameConstantsName = "PostProcessFrameConstants";
	constexpr const char* kParameterConstantsName = "PostProcessParameters";
	constexpr const char* kSourceColorName = "gSourceColor";
	constexpr const char* kSourceDepthName = "gSourceDepth";
	constexpr const char* kDestColorName = "gDestColor";

	bool IsResourceBinding(const Engine::ShaderResourceBinding& binding) {

		return binding.kind == Engine::ShaderBindingKind::SRV ||
			binding.kind == Engine::ShaderBindingKind::UAV;
	}

	bool RequiresSourceDepth(const Engine::PipelineState& pipelineState) {

		if (pipelineState.FindBindingByName(kSourceDepthName, Engine::ShaderBindingKind::SRV)) {
			return true;
		}
		return pipelineState.FindBinding(Engine::ShaderBindingKind::SRV, 1, 0) != nullptr;
	}

	std::string MakePostProcessLogHeader(const Engine::MaterialAsset& material,
		const Engine::PostProcessExecutionDesc& desc) {

		return "[PostProcess] material=" + material.name + " pass=" + desc.passName + " ";
	}

	Engine::RenderTexture2D* GetFirstColor(Engine::MultiRenderTarget* target) {

		if (!target || target->GetColorCount() == 0) {
			return nullptr;
		}
		return target->GetColorTexture(0);
	}

	Engine::MultiRenderTarget* ResolveExtraSource(const Engine::SceneExecutionContext& context,
		const std::string& targetName) {

		if (!context.targetRegistry || targetName.empty()) {
			return nullptr;
		}
		if (Engine::MultiRenderTarget* byAlias = context.targetRegistry->Find(targetName)) {
			return byAlias;
		}

		Engine::RenderTargetSetReference reference{};
		reference.colors = { targetName };
		return context.targetRegistry->Resolve(reference);
	}

	bool AppendSRVBinding(const Engine::ShaderResourceBinding& binding,
		Engine::GraphicsCore& graphicsCore, const Engine::SceneExecutionContext& context,
		const Engine::PostProcessExecutionDesc& desc, Engine::MultiRenderTarget& source,
		std::vector<Engine::ComputeBindItem>& outBindItems, const std::string& logHeader) {

		Engine::RenderTexture2D* sourceColor = GetFirstColor(&source);
		Engine::DepthTexture2D* sourceDepth = source.GetDepthTexture();
		Engine::RenderTexture2D* texture = nullptr;
		Engine::DepthTexture2D* depth = nullptr;
		std::string resolvedName{};

		// 名前が取れている場合は、標準名を優先して解決する。
		if (binding.name == kSourceColorName) {

			texture = sourceColor;
			resolvedName = kSourceColorName;
		} else if (binding.name == kSourceDepthName) {

			depth = sourceDepth;
			resolvedName = kSourceDepthName;
		} else if (!binding.name.empty()) {

			auto found = desc.extraSources.find(binding.name);
			if (found != desc.extraSources.end()) {

				Engine::MultiRenderTarget* extra = ResolveExtraSource(context, found->second);
				texture = GetFirstColor(extra);
				resolvedName = binding.name;
			}
		}

		// 古いshaderや無名binding向けに、標準register規約も残す。
		if (!texture && !depth) {
			if (binding.bindPoint == 0 && binding.space == 0) {
				texture = sourceColor;
				resolvedName = kSourceColorName;
			} else if (binding.bindPoint == 1 && binding.space == 0) {
				depth = sourceDepth;
				resolvedName = kSourceDepthName;
			}
		}

		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		if (texture) {

			texture->Transition(*dxCommand,
				static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

			outBindItems.push_back({ binding.name.empty() ? std::string_view{} : std::string_view(binding.name),
				Engine::ComputeBindValueType::SRV, 0, texture->GetSRVGPUHandle(),
				binding.bindPoint, binding.space });
			return true;
		}
		if (depth) {

			depth->Transition(*dxCommand,
				static_cast<D3D12_RESOURCE_STATES>(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
					D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

			outBindItems.push_back({ binding.name.empty() ? std::string_view{} : std::string_view(binding.name),
				Engine::ComputeBindValueType::SRV, 0, depth->GetSRVGPUHandle(),
				binding.bindPoint, binding.space });
			return true;
		}

		Engine::Logger::Output(Engine::LogType::Engine, logHeader + "unresolved SRV binding. binding=" +
			(binding.name.empty() ? std::to_string(binding.bindPoint) : binding.name));
		return false;
	}

	bool AppendUAVBinding(const Engine::ShaderResourceBinding& binding,
		Engine::GraphicsCore& graphicsCore, Engine::MultiRenderTarget& dest,
		std::vector<Engine::ComputeBindItem>& outBindItems, const std::string& logHeader) {

		Engine::RenderTexture2D* destColor = GetFirstColor(&dest);
		if (!destColor) {
			Engine::Logger::Output(Engine::LogType::Engine, logHeader + "destination color is missing.");
			return false;
		}
		if (destColor->GetUAVGPUHandle().ptr == 0) {
			Engine::Logger::Output(Engine::LogType::Engine, logHeader + "destination color has no UAV descriptor.");
			return false;
		}

		const bool isDestColor =
			(binding.name == kDestColorName) ||
			(binding.name.empty() && binding.bindPoint == 0 && binding.space == 0);
		if (!isDestColor) {
			Engine::Logger::Output(Engine::LogType::Engine, logHeader + "unresolved UAV binding. binding=" + binding.name);
			return false;
		}

		destColor->Transition(*graphicsCore.GetDXObject().GetDxCommand(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		outBindItems.push_back({ binding.name.empty() ? std::string_view{} : std::string_view(binding.name),
			Engine::ComputeBindValueType::UAV, 0, destColor->GetUAVGPUHandle(),
			binding.bindPoint, binding.space });
		return true;
	}
}

void Engine::PostProcessExecutor::BeginFrame(float deltaTime) {

	elapsedTime_ += deltaTime;
	++frameIndex_;
	constantBufferAllocator_.BeginFrame();
}

void Engine::PostProcessExecutor::Release() {

	constantBufferAllocator_.Release();
	parameterLayoutCache_.clear();
	elapsedTime_ = 0.0f;
	frameIndex_ = 0;
}

bool Engine::PostProcessExecutor::Execute(GraphicsCore& graphicsCore, const RenderFrameRequest& request,
	const SceneExecutionContext& context, RenderAssetLibrary& assetLibrary,
	PipelineStateCache& pipelineCache, const PostProcessExecutionDesc& desc) {

	(void)request;

	if (!context.targetRegistry || !context.assetDatabase) {
		return false;
	}

	// レンダーターゲットを取得
	MultiRenderTarget* source = context.targetRegistry->Resolve(desc.source);
	MultiRenderTarget* dest = context.targetRegistry->Resolve(desc.dest);
	RenderTexture2D* sourceColor = GetFirstColor(source);
	RenderTexture2D* destColor = GetFirstColor(dest);
	if (!source || !dest || !sourceColor || !destColor) {
		return false;
	}

	//============================================================================
	//	ポストプロセスの実行に必要なアセットの取得
	//============================================================================

	// ポストプロセスマテリアル取得
	const MaterialAsset* materialAsset = assetLibrary.LoadMaterial(desc.material);
	if (!materialAsset) {
		Logger::Output(LogType::Engine, "[PostProcess] material is missing.");
		return false;
	}

	// これから実行されるポスト識別名を取得
	const std::string logHeader = MakePostProcessLogHeader(*materialAsset, desc);

	// 書き込み先と書き込み元が同じなら処理しない
	if (sourceColor->GetResource() == destColor->GetResource()) {
		Logger::Output(LogType::Engine, logHeader + "source and dest are same resource. In-place compute is not allowed.");
		return false;
	}

	// 描画パスをマテリアルから取得
	const MaterialPassBinding* passBinding = FindPass(*materialAsset, desc.passName);
	if (!passBinding) {
		passBinding = FindPass(*materialAsset, "PostProcess");
	}
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Compute) {
		Logger::Output(LogType::Engine, logHeader + "compute pass is missing.");
		return false;
	}

	// パイプラインを取得
	const PipelineState* pipelineState = pipelineCache.GetORCreate(graphicsCore.GetDXObject(),
		assetLibrary, passBinding->pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetComputePipeline()) {
		Logger::Output(LogType::Engine, logHeader + "pipeline is missing or compile failed.");
		return false;
	}

	// 深度リソースをポストプロセスで使用するか
	const bool requiresDepth = RequiresSourceDepth(*pipelineState);
	// 使用するのに深度リソースがない場合
	if (requiresDepth && !source->GetDepthTexture()) {
		Logger::Output(LogType::Engine, logHeader + "requires gSourceDepth, but source depth is missing.");
		return false;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	//============================================================================
	//	パイプライン・描画バインディング
	//============================================================================

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });
	commandList->SetComputeRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetComputePipeline());

	std::vector<ComputeBindItem> binds{};
	binds.reserve(8);
	// リフレクション情報からバッファをバインドするa
	const ShaderReflectionInfo& reflection = pipelineState->GetComputeReflection();
	for (const ShaderResourceBinding& binding : reflection.resources) {
		if (!IsResourceBinding(binding)) {
			continue;
		}

		if (binding.kind == ShaderBindingKind::SRV) {
			if (!AppendSRVBinding(binding, graphicsCore, context, desc, *source, binds, logHeader)) {
				return false;
			}
			continue;
		}
		if (binding.kind == ShaderBindingKind::UAV) {
			if (!AppendUAVBinding(binding, graphicsCore, *dest, binds, logHeader)) {
				return false;
			}
			continue;
		}
	}

	bool hasFrameConstantsByName = (pipelineState->FindBindingByName(kFrameConstantsName, ShaderBindingKind::CBV) != nullptr);
	// 共通フレーム、ポストプロセス実行情報のバッファデータを構築してバインド
	if (hasFrameConstantsByName || pipelineState->FindBinding(ShaderBindingKind::CBV, 0, 0)) {

		PostProcessFrameConstants constants{};
		constants.resolution = Vector2(static_cast<float>(dest->GetWidth()), static_cast<float>(dest->GetHeight()));
		constants.invResolution = Vector2(1.0f / (std::max)(constants.resolution.x, 1.0f), 1.0f / (std::max)(constants.resolution.y, 1.0f));
		constants.time = elapsedTime_;
		constants.deltaTime = context.systemContext->deltaTime;
		constants.frameIndex = frameIndex_;

		auto allocation = constantBufferAllocator_.AllocateAndUpload(graphicsCore.GetDXObject().GetDevice(), constants);
		binds.push_back({ hasFrameConstantsByName ? std::string_view(kFrameConstantsName) : std::string_view{},
			ComputeBindValueType::CBV, allocation.gpuAddress, {}, 0, 0 });
	}

	// ポストプロセス固有のパラメータバッファの構築
	auto layoutIt = parameterLayoutCache_.find(pipelineState);
	if (layoutIt == parameterLayoutCache_.end()) {

		PostProcessParameterLayout layout{};
		layout.Build(reflection);
		layoutIt = parameterLayoutCache_.emplace(pipelineState, std::move(layout)).first;
	}
	// バッファがあればバインド
	PostProcessParameterLayout& parameterLayout = layoutIt->second;
	if (parameterLayout.IsValid()) {

		std::vector<uint8_t> bytes = PostProcessParameterBufferBuilder::Build(*materialAsset, parameterLayout);
		auto allocation = constantBufferAllocator_.AllocateAndUploadBytes(graphicsCore.GetDXObject().GetDevice(), bytes);

		binds.push_back({ kParameterConstantsName, ComputeBindValueType::CBV,
			allocation.gpuAddress, {}, parameterLayout.GetBindPoint(), parameterLayout.GetSpace() });
	}
	// 追加されたバインドデータをバインディング
	ComputeRootBinder binder{ *pipelineState };
	binder.Bind(commandList, binds);

	// ポストプロセス実行
	UINT dispatchX = 1;
	UINT dispatchY = 1;
	UINT dispatchZ = 1;
	if (desc.dispatchMode == ComputeDispatchMode::Fixed) {

		dispatchX = (std::max)(desc.groupCountX, 1u);
		dispatchY = (std::max)(desc.groupCountY, 1u);
		dispatchZ = (std::max)(desc.groupCountZ, 1u);
	} else {

		const uint32_t width = (desc.dispatchMode == ComputeDispatchMode::FromSourceSize) ? source->GetWidth() : dest->GetWidth();
		const uint32_t height = (desc.dispatchMode == ComputeDispatchMode::FromSourceSize) ? source->GetHeight() : dest->GetHeight();
		dispatchX = DxUtils::RoundUp(width, pipelineState->GetThreadGroupX());
		dispatchY = DxUtils::RoundUp(height, pipelineState->GetThreadGroupY());
		dispatchZ = 1;
	}
	commandList->Dispatch(dispatchX, dispatchY, dispatchZ);

	// UAVバリアを張る
	const D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(destColor->GetResource());
	commandList->ResourceBarrier(1, &uavBarrier);
	dest->TransitionForShaderRead(*dxCommand);

	return true;
}