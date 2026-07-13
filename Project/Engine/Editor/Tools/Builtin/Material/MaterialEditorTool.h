#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

// directX
#include <d3d12.h>
// c++
#include <filesystem>
#include <string>

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
		FillFaceMesh,
	};

	//============================================================================
	//	PipelineCreateSettings struct
	//	パイプライン生成時にエディタで編集する設定
	//============================================================================
	struct PipelineCreateSettings {

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

		// 静的サンプラー、U/V/Wは同じ設定をまとめて使う
		D3D12_FILTER samplerFilter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		D3D12_TEXTURE_ADDRESS_MODE samplerAddress = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	};

	//============================================================================
	//	MaterialEditorTool class
	//	描画タイプごとのデフォルトマテリアル設定とマテリアル/パイプラインの作成を行うツール
	//============================================================================
	class MaterialEditorTool :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MaterialEditorTool() = default;
		~MaterialEditorTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// Materialツールウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// ToolPanelへ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.material_editor",
			.name = "マテリアル作成",
			.category = "レンダリング",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 1,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;

		// マテリアル作成セクションの入力状態
		MaterialCreateType createType_ = MaterialCreateType::Mesh;
		bool typeDefaultsInitialized_ = false;
		AssetID createVS_{};
		AssetID createPS_{};
		AssetID createMS_{};
		AssetID createAS_{};
		// Lineタイプで使うジオメトリシェーダー
		AssetID createGS_{};
		// 既存マテリアルからパイプライン設定を取り込む元、生成自体には使わない
		AssetID createSourceMaterial_{};
		// 取り込み元マテリアルの参照シェーダーも一緒に設定するか
		bool createImportShaders_ = false;
		// GameAssets/以降のパスでファイル名込み、拡張子は付けない
		std::string createRelativePath_{};
		PipelineCreateSettings createPipeline_{};
		// 作成結果のフィードバック
		std::string createMessage_{};

		// マテリアルパラメータ編集状態
		AssetID editParameterMaterial_{};
		MaterialAsset editParameterDraft_{};
		bool editParameterDraftValid_ = false;
		bool editParameterDirty_ = false;
		std::string editParameterMessage_{};

		// Particle PSソース編集状態
		AssetID editParticleMaterial_{};
		AssetID editShaderAsset_{};
		std::filesystem::path editShaderSourcePath_{};
		std::string editShaderSource_{};
		std::string editShaderOriginal_{};
		std::string editShaderEntry_ = "main";
		std::string editShaderProfile_ = "ps_6_6";
		std::string editShaderMessage_{};

		//--------- functions ----------------------------------------------------

		// Materialツールウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
		// 描画タイプごとのデフォルトマテリアル設定セクションを描画する
		void DrawDefaultMaterialSection(const EditorToolContext& context);
		// マテリアル/パイプライン作成セクションを描画する
		void DrawCreateMaterialSection(const EditorToolContext& context);
		// Particle PSのソース編集と保存を描画する
		void DrawParticleShaderSection(const EditorToolContext& context);
		// reflectionからマテリアルパラメータの編集UIを描画する
		void DrawMaterialParameterSection(const EditorToolContext& context);
		// タイプ変更時にパイプライン設定の既定値を適用する
		void ApplyTypeDefaults(MaterialCreateType type);
		// 既存マテリアルが参照するpipelineからパイプライン設定を読み取り編集欄へ反映する
		void LoadPipelineSettingsFromMaterial(AssetDatabase& assetDatabase, AssetID materialID);
		// 既存マテリアルが参照するshaderから各ステージのhlslを読み取り作成欄へ反映する
		void LoadShadersFromMaterial(AssetDatabase& assetDatabase, AssetID materialID);
		// 入力内容からshader/pipeline/materialの3ファイルを生成する
		bool CreateMaterialAssets(const EditorToolContext& context);
		// Materialの部分PSソースを読み込む
		bool LoadParticleShaderSource(const EditorToolContext& context);
		// PSを検証して保存し描画へ反映する
		bool SaveParticleShaderSource(const EditorToolContext& context);
		// 編集対象マテリアルを読み込む
		bool LoadMaterialParameters(const EditorToolContext& context, AssetID materialID);
		// 編集したマテリアルパラメータを保存する
		bool SaveMaterialParameters(const EditorToolContext& context);
	};
} // Engine
