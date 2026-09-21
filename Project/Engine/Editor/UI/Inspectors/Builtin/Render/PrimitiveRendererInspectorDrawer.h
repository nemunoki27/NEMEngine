#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/SerializedComponentInspectorDrawer.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>

namespace Engine {

	// front
	struct ShaderReflectionInfo;
	struct ShaderConstantBufferVariable;

	//============================================================================
	//	PrimitiveRendererInspectorDrawer class
	//	PrimitiveRendererComponentのインスペクター描画
	//============================================================================
	class PrimitiveRendererInspectorDrawer :
		public SerializedComponentInspectorDrawer<PrimitiveRendererComponent> {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PrimitiveRendererInspectorDrawer() : SerializedComponentInspectorDrawer("Primitive Renderer", "PrimitiveRenderer") {}
		~PrimitiveRendererInspectorDrawer() = default;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// マテリアル既定値とreflection解決のためのキャッシュ
		AssetID cachedMaterialID_{};
		MaterialAsset cachedMaterial_{};
		bool cachedMaterialValid_ = false;

		//--------- functions ----------------------------------------------------

		void DrawFields(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool& anyItemActive) override;

		// マテリアルのDrawパスreflectionを解決しキャッシュする、失敗時はnullptr
		// 空マテリアルはdefaultMaterialIDへ解決してからreflectionを引く
		const ShaderReflectionInfo* EnsureMaterialReflection(const EditorPanelContext& context,
			AssetID materialID, AssetID defaultMaterialID);
		// param最終値を解決する、上書き無しはマテリアル既定値か型既定値
		MaterialParameterValue ResolveParamValue(const PrimitiveRendererComponent& draft,
			const ShaderConstantBufferVariable& var) const;
		// シェーダーreflection駆動でマテリアルパラメータを編集する
		void DrawReflectedParameters(const EditorPanelContext& context,
			PrimitiveRendererComponent& draft, bool& anyItemActive);
	};
} // Engine
