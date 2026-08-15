#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessConstantBufferAllocator.h>
#include <Engine/Core/Rendering/Materials/MaterialParameterLayout.h>
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

// c++
#include <unordered_map>

namespace Engine {

	// front
	class GraphicsCore;
	class PipelineState;
	class PipelineStateCache;
	class RenderAssetLibrary;
	struct RenderFrameRequest;
	struct SceneExecutionContext;

	//============================================================================
	//	PostProcess structures
	//============================================================================
	struct PostProcessFrameConstants {

		// 解像度情報
		Vector2 resolution;
		Vector2 invResolution;

		// 起動してからの経過時間
		float time;
		float deltaTime;
		// 経過フレーム
		uint32_t frameIndex;
		float _pad0;

		// カメラ情報
		// 近/遠クリップ距離
		float cameraNear;
		float cameraFar;
		float _pad1;
		float _pad2;

		// ワールド座標
		Vector3 cameraWorldPos;
		float _pad3;

		// ビュー情報
		Matrix4x4 cameraView;
		Matrix4x4 cameraViewInverse;
		Matrix4x4 cameraProjection;
		Matrix4x4 cameraProjectionInverse;
	};

	enum class ComputeDispatchMode : uint8_t {

		FromDestSize,
		FromSourceSize,
		Fixed,
	};

	struct PostProcessExecutionDesc {

		AssetID material{};
		MaterialPassKind passKind = MaterialPassKind::PostProcess;
		RenderTargetSetReference source;
		RenderTargetSetReference dest;
		std::unordered_map<std::string, std::string> extraSources;
		std::unordered_map<std::string, std::string> outputTargets;
		MaterialParameterSet parameterOverrides;
		std::unordered_map<std::string, AssetID> textureOverrides;
		std::unordered_map<std::string, PipelineStaticSamplerSettings> samplerOverrides;
		ComputeDispatchMode dispatchMode = ComputeDispatchMode::FromDestSize;
		uint32_t groupCountX = 1;
		uint32_t groupCountY = 1;
		uint32_t groupCountZ = 1;
	};

	//============================================================================
	//	PostProcessExecutor class
	// ComputeShader版PostProcessの実行を担当するクラス
	//============================================================================
	class PostProcessExecutor {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PostProcessExecutor() = default;
		~PostProcessExecutor() = default;

		// フレームごとの時間情報を更新する
		void BeginFrame(float deltaTime);
		// 内部バッファを破棄する
		void Release();

		// Compute PostProcessを実行する
		bool Execute(GraphicsCore& graphicsCore, const RenderFrameRequest& request,
			const SceneExecutionContext& context, RenderAssetLibrary& assetLibrary,
			PipelineStateCache& pipelineCache, const PostProcessExecutionDesc& desc);

		// 実行なしでマテリアルのリフレクション情報を取得する
		bool TryGetReflection(GraphicsCore& graphicsCore, RenderAssetLibrary& assetLibrary,
			PipelineStateCache& pipelineCache, AssetID materialID, MaterialPassKind passKind,
			std::vector<ShaderConstantBufferVariable>& outVars,
			std::vector<ShaderResourceBinding>& outSRVs,
			std::vector<ShaderResourceBinding>& outSamplers);

		//--------- accessor -----------------------------------------------------

		// パラメータレイアウトキャッシュを全削除するシェーダーリロード時に呼ぶ処理
		void ClearParameterLayoutCache() { parameterLayoutCache_.clear(); }

		// 最後に実行されたマテリアルのIDを取得する
		AssetID GetLastExecutedMaterial() const { return lastExecutedMaterial_; }
		// 最後に実行されたパラメータレイアウトを取得する
		const MaterialParameterLayout* GetLastExecutedLayout() const { return lastExecutedLayout_; }
		// 最後に実行されたSRVバインディングを取得する
		const std::vector<ShaderResourceBinding>& GetLastExecutedSRVBindings() const { return lastExecutedSRVBindings_; }
		// 最後に実行されたSamplerバインディングを取得する
		const std::vector<ShaderResourceBinding>& GetLastExecutedSamplerBindings() const { return lastExecutedSamplerBindings_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// パイプラインごとのキャッシュエントリでフレーム定数バインドの有無も初回のみ解決して保持する
		struct PipelineCacheEntry {

			MaterialParameterLayout layout;
			bool hasFrameConstantsByName = false;
			bool hasFrameConstantsByRegister = false;
		};

		//--------- variables ----------------------------------------------------

		PostProcessConstantBufferAllocator constantBufferAllocator_{};
		std::unordered_map<const PipelineState*, PipelineCacheEntry> parameterLayoutCache_{};
		float elapsedTime_ = 0.0f;
		uint32_t frameIndex_ = 0;

		AssetID lastExecutedMaterial_{};
		const MaterialParameterLayout* lastExecutedLayout_ = nullptr;
		std::vector<ShaderResourceBinding> lastExecutedSRVBindings_{};
		std::vector<ShaderResourceBinding> lastExecutedSamplerBindings_{};
	};
} // Engine
