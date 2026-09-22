#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// c++
#include <string>

// directX
#include <d3d12.h>

namespace Engine {

	//============================================================================
	//	MaterialCreateType enum
	//	マテリアル作成で対象にする描画タイプ
	//============================================================================
	enum class MaterialCreateType {

		Mesh,
		Particle,
		Sprite,
		Text,
		Line,
	};

	//============================================================================
	//	PipelineCreateSettings struct
	//	パイプライン生成時にエディタで編集する設定
	//============================================================================
	struct PipelineCreateSettings {

		MaterialSurfaceMode surfaceMode = MaterialSurfaceMode::Opaque;
		BlendMode blendMode = BlendMode::Normal;

		// ラスタライザ
		D3D12_FILL_MODE fillMode = D3D12_FILL_MODE_SOLID;
		D3D12_CULL_MODE cullMode = D3D12_CULL_MODE_NONE;
		bool frontCounterClockwise = false;
		bool depthClipEnable = true;

		// 深度ステンシル
		bool depthEnable = false;
		D3D12_DEPTH_WRITE_MASK depthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		D3D12_COMPARISON_FUNC depthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		bool stencilEnable = false;

		// 静的サンプラー
		D3D12_FILTER samplerFilter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		D3D12_TEXTURE_ADDRESS_MODE samplerAddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		D3D12_TEXTURE_ADDRESS_MODE samplerAddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		D3D12_TEXTURE_ADDRESS_MODE samplerAddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		D3D12_COMPARISON_FUNC samplerComparison = D3D12_COMPARISON_FUNC_ALWAYS;
		D3D12_STATIC_BORDER_COLOR samplerBorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		int32_t samplerMaxAnisotropy = 1;
		float samplerMipLODBias = 0.0f;
		float samplerMinLOD = 0.0f;
		float samplerMaxLOD = D3D12_FLOAT32_MAX;
	};

	struct MaterialCreationDraft {

		// マテリアル作成セクションの入力状態
		MaterialCreateType createType = MaterialCreateType::Mesh;
		// 不透明描画で使うシェーダーステージ
		AssetID createVS{};
		AssetID createPS{};
		AssetID createMS{};
		AssetID createAS{};
		// 不透明PSのエントリーポイント
		std::string createPSEntry = "main";
		// Meshの半透明パスを別Pipelineで生成するか
		bool createTransparentPass = true;
		// 半透明描画で使うPS
		AssetID createTransparentPS{};
		// 半透明PSのエントリーポイント
		std::string createTransparentPSEntry = "mainTransparent";
		// Lineタイプで使うジオメトリシェーダー
		AssetID createGS{};
		// 既存マテリアルからパイプライン設定を取り込む元、生成自体には使わない
		AssetID createSourceMaterial{};
		// 取り込み元マテリアルの参照シェーダーも一緒に設定するか
		bool createImportShaders = false;
		// Shader Graph Materialの描画状態はGraph側でのみ編集する
		bool createSourceUsesShaderGraph = false;
		// GameAssets/以降のパスでファイル名込み、拡張子は付けない
		std::string createRelativePath{};
		// 不透明描画用のPipeline設定
		PipelineCreateSettings createPipeline{};
		// 半透明描画用のPipeline設定
		PipelineCreateSettings createTransparentPipeline{};
		// 作成結果のフィードバック
		std::string createMessage{};

	};

	class AssetDatabase;
	struct EditorToolContext;

	//============================================================================
	//	MaterialCreationSession class
	//	Material生成の入力と既存設定の取込と成果物作成を管理するクラス
	//============================================================================
	class MaterialCreationSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void ApplyTypeDefaults(MaterialCreateType type);
		void LoadPipelineSettingsFromMaterial(AssetDatabase& assetDatabase, AssetID materialID);
		void LoadShadersFromMaterial(AssetDatabase& assetDatabase, AssetID materialID);
		bool CreateMaterialAssets(const EditorToolContext& context);

		//--------- accessor -----------------------------------------------------

		MaterialCreationDraft& GetDraft() { return draft_; }
		const MaterialCreationDraft& GetDraft() const { return draft_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		MaterialCreationDraft draft_;
	};
}
