#include "PostProcessExecutor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessBindingUtility.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>
#include <Engine/Core/Rendering/Pipelines/PipelineStateCache.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterBufferBuilder.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetRegistry.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
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
	const MaterialPassBinding* passBinding = FindPass(*materialAsset, desc.passKind);
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

	// パイプラインキャッシュを解決し、フレーム定数バインドの有無も初回のみ解決してキャッシュする
	auto layoutIt = parameterLayoutCache_.find(pipelineState);
	if (layoutIt == parameterLayoutCache_.end()) {

		PipelineCacheEntry entry{};
		entry.layout.Build(reflection, "PostProcessParameters");
		entry.hasFrameConstantsByName = (pipelineState->FindBindingByName(kFrameConstantsName, ShaderBindingKind::CBV) != nullptr);
		entry.hasFrameConstantsByRegister = (pipelineState->FindBinding(ShaderBindingKind::CBV, 0, 0) != nullptr);
		layoutIt = parameterLayoutCache_.emplace(pipelineState, std::move(entry)).first;
	}
	PipelineCacheEntry& cacheEntry = layoutIt->second;

	// 共通フレーム、ポストプロセス実行情報のバッファデータを構築してバインド
	if (cacheEntry.hasFrameConstantsByName || cacheEntry.hasFrameConstantsByRegister) {

		PostProcessFrameConstants constants{};
		constants.resolution = Vector2(static_cast<float>(dest->GetWidth()), static_cast<float>(dest->GetHeight()));
		constants.invResolution = Vector2(1.0f / (std::max)(constants.resolution.x, 1.0f), 1.0f / (std::max)(constants.resolution.y, 1.0f));
		constants.time = elapsedTime_;
		constants.deltaTime = context.systemContext->deltaTime;
		constants.frameIndex = frameIndex_;

		auto allocation = constantBufferAllocator_.AllocateAndUpload(graphicsCore.GetDXObject().GetDevice(), constants);
		binds.push_back({ cacheEntry.hasFrameConstantsByName ? std::string_view(kFrameConstantsName) : std::string_view{},
			ComputeBindValueType::CBV, allocation.gpuAddress, {}, 0, 0 });
	}

	// ポストプロセス固有のパラメータバッファの構築
	// バッファがあればバインド
	MaterialParameterLayout& parameterLayout = cacheEntry.layout;
	if (parameterLayout.IsValid()) {

		std::vector<uint8_t> bytes;
		if (!desc.parameterOverrides.empty()) {

			MaterialAsset merged = *materialAsset;
			for (const auto& [name, val] : desc.parameterOverrides) {
				merged.parameters[name] = val;
			}
			bytes = MaterialParameterBufferBuilder::Build(merged, parameterLayout);
		} else {
			bytes = MaterialParameterBufferBuilder::Build(*materialAsset, parameterLayout);
		}

		auto allocation = constantBufferAllocator_.AllocateAndUploadBytes(graphicsCore.GetDXObject().GetDevice(), bytes);

		binds.push_back({ kParameterConstantsName, ComputeBindValueType::CBV,
			allocation.gpuAddress, {}, parameterLayout.GetBindPoint(), parameterLayout.GetSpace() });
	}

	lastExecutedMaterial_ = desc.material;
	lastExecutedLayout_ = parameterLayout.IsValid() ? &parameterLayout : nullptr;
	lastExecutedSRVBindings_.clear();
	for (const auto& binding : reflection.resources) {
		if (binding.kind == ShaderBindingKind::SRV &&
			binding.name != kSourceColorName &&
			binding.name != kSourceDepthName) {
			lastExecutedSRVBindings_.push_back(binding);
		}
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

bool Engine::PostProcessExecutor::TryGetReflection(GraphicsCore& graphicsCore,
	RenderAssetLibrary& assetLibrary, PipelineStateCache& pipelineCache,
	AssetID materialId, MaterialPassKind passKind,
	std::vector<ShaderConstantBufferVariable>& outVars,
	std::vector<ShaderResourceBinding>& outSRVs) {

	const MaterialAsset* materialAsset = assetLibrary.LoadMaterial(materialId);
	if (!materialAsset) {
		return false;
	}

	const MaterialPassBinding* passBinding = FindPass(*materialAsset, passKind);
	if (!passBinding || passBinding->preferredVariant != PipelineVariantKind::Compute) {
		return false;
	}

	const PipelineState* pipelineState = pipelineCache.GetORCreate(graphicsCore.GetDXObject(),
		assetLibrary, passBinding->pipeline, PipelineVariantKind::Compute, {}, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetComputePipeline()) {
		return false;
	}

	const ShaderReflectionInfo& reflection = pipelineState->GetComputeReflection();

	// PostProcessParameters CBufferの変数一覧を取得する
	auto layoutIt = parameterLayoutCache_.find(pipelineState);
	if (layoutIt == parameterLayoutCache_.end()) {
		PipelineCacheEntry entry{};
		entry.layout.Build(reflection, "PostProcessParameters");
		entry.hasFrameConstantsByName = (pipelineState->FindBindingByName(kFrameConstantsName, ShaderBindingKind::CBV) != nullptr);
		entry.hasFrameConstantsByRegister = (pipelineState->FindBinding(ShaderBindingKind::CBV, 0, 0) != nullptr);
		layoutIt = parameterLayoutCache_.emplace(pipelineState, std::move(entry)).first;
	}
	outVars = layoutIt->second.layout.GetVariables();

	// gSourceColor / gSourceDepthを除くユーザー向けSRVを収集する
	outSRVs.clear();
	for (const auto& binding : reflection.resources) {
		if (binding.kind == ShaderBindingKind::SRV &&
			binding.name != kSourceColorName &&
			binding.name != kSourceDepthName) {
			outSRVs.push_back(binding);
		}
	}

	return true;
}
