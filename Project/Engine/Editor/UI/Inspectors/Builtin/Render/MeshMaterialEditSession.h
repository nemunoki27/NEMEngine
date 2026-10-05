#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/MaterialReflectionCache.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace Engine {

	struct EditorPanelContext;
	struct ValueEditResult;
	class MaterialParameterLayout;

	//============================================================================
	//	MeshMaterialEditSession class
	//	SubMeshのMaterial表示と同時編集を管理する
	//============================================================================
	class MeshMaterialEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Material割当と表面方式を編集する
		ValueEditResult DrawMaterialFields(
			const EditorPanelContext& context, const MeshRendererComponent& renderer, SubMeshMaterial& subMesh);
		// 共通の値を全SubMeshへ適用する
		ValueEditResult DrawBatch(
			const EditorPanelContext& context, const MeshRendererComponent& draft, std::span<SubMeshMaterial> subMeshes);
		// Shaderの公開値を編集する
		ValueEditResult DrawParameters(const EditorPanelContext& context, AssetID materialID, SubMeshMaterial& subMesh);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Materialの既定値とreflectionのcache
		MaterialReflectionCache materialReflection_;
		// Materialの同時編集
		bool batchEditSubMeshMaterials_ = false;
		// 混在値の上書きを許可した名前
		std::unordered_set<std::string> batchOverrideAllowed_{};

		//--------- functions ----------------------------------------------------

		// Materialとモデル情報から実効表面方式を求める
		static MaterialSurfaceMode ResolveSubMeshSurfaceMode(
			const EditorPanelContext& context, const MeshRendererComponent& renderer, const SubMeshMaterial& subMesh);
		// 公開値をScalarとTextureへ分類する
		static void CollectParameters(const ShaderReflectionInfo& reflection, const MaterialParameterLayout& layout,
			std::vector<const ShaderConstantBufferVariable*>& scalars,
			std::vector<const ShaderConstantBufferVariable*>& textures);
	};
} // Engine
