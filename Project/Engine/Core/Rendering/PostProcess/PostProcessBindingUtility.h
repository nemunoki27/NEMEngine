#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/ComputeRootBinder.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessExecutor.h>
#include <string>
#include <vector>

namespace Engine {

	struct SceneExecutionContext;

	//============================================================================
	//	PostProcessBindingUtility functions
	//	ポストプロセスの実行に必要なリソースバインディングのユーティリティ関数群
	//============================================================================

	// リソースバインディングの種類がSRVまたはUAVか確認
	bool IsResourceBinding(const ShaderResourceBinding& binding);

	// パイプラインがソース深度テクスチャを必要としているか判定
	bool RequiresSourceDepth(const PipelineState& pipelineState);

	// ポストプロセス実行時のログヘッダーを作成
	std::string MakePostProcessLogHeader(const MaterialAsset& material, const PostProcessExecutionDesc& desc);

	// MRTの最初のカラーテクスチャを取得
	RenderTexture2D* GetFirstColor(MultiRenderTarget* target);

	// 指定された名前のレンダーターゲットを解決
	MultiRenderTarget* ResolveExtraSource(const SceneExecutionContext& context, const std::string& targetName);
	// sourceがカラー専用の場合も追加入力から標準深度を解決する
	DepthTexture2D* ResolveSourceDepth(const SceneExecutionContext& context,
		const PostProcessExecutionDesc& desc, MultiRenderTarget& source);

	// SRVバインディングを追加し標準名のgSourceColorやgSourceDepthやオーバーライドを考慮する
	bool AppendSRVBinding(const ShaderResourceBinding& binding,
		GraphicsCore& graphicsCore, const SceneExecutionContext& context,
		const PostProcessExecutionDesc& desc, MultiRenderTarget& source,
		std::vector<ComputeBindItem>& outBindItems, const std::string& logHeader);

	// UAVバインディングを追加し出力先のgDestColorをバインドする
	bool AppendUAVBinding(const ShaderResourceBinding& binding,
		GraphicsCore& graphicsCore, const SceneExecutionContext& context,
		const PostProcessExecutionDesc& desc, MultiRenderTarget& dest,
		std::vector<ComputeBindItem>& outBindItems, const std::string& logHeader);

} // Engine
