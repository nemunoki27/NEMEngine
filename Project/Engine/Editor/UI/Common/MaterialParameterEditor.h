#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Stage/ShaderReflection.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>

//============================================================================
//	MaterialParameterEditor
//	反射情報からMaterialの編集UIを作る
//============================================================================
namespace Engine::MaterialParameterEditor {

	bool DrawReloadButton(bool hasOverrides);
	ValueEditResult DrawReload(MaterialParameterSet& parameters);

	// 値編集と同じ名前とIDをクリップボードへコピーする
	MyGUI::ScopedPropertyLabelContextMenu MakeLabelContextMenu(MaterialParameterID parameterID, std::string_view name);

	// 標準PBRスカラーをインスペクターで扱いやすい順に並べる
	size_t GetScalarDisplayRank(const ShaderConstantBufferVariable& var);

	// 標準PBRテクスチャを通常パラメータの下で固定順に並べる
	size_t GetTextureDisplayRank(MaterialParameterSemantic semantic, std::string_view name);

	size_t GetTextureDisplayRank(std::string_view name);

	void SortScalarParametersForDisplay(std::vector<const ShaderConstantBufferVariable*>& variables);

	void SortTextureParametersForDisplay(std::vector<const ShaderConstantBufferVariable*>& variables);

	// HLSLの16byte整列用メンバはユーザー編集対象にしない
	bool IsInternalPaddingParameter(const ShaderConstantBufferVariable& var);
	// 非公開の内部値を編集欄から除外する
	bool IsExposedParameter(const ShaderConstantBufferVariable& variable);
	// 最適化で消えた内部値も公開対象か判定する
	bool IsExposedParameter(MaterialParameterID id, const ShaderReflectionInfo& reflection);
	// 非公開のTextureを編集欄から除外する
	bool IsExposedTextureResource(const ShaderResourceBinding& resource, const ShaderReflectionInfo& reflection);

	// メタデータまたは変数名から色パラメータか判定する
	bool IsColorParameter(const ShaderConstantBufferVariable& var);

	// space2のTexture SRVだけをマテリアルテクスチャとして扱う
	bool IsMaterialTextureResource(const ShaderResourceBinding& resource);

	bool IsTextureSemantic(MaterialParameterSemantic semantic);

	// Shader Graphの公開IDを優先してcbuffer変数とTexture SRVを対応付ける
	bool IsSameReflectedMaterialParameter(const ShaderConstantBufferVariable& variable,
		const ShaderResourceBinding& resource);

	const ShaderResourceBinding* FindReflectedTextureResource(const ShaderConstantBufferVariable& variable,
		const ShaderReflectionInfo& reflection);

	const ShaderConstantBufferVariable* FindReflectedTextureParameter(const ShaderResourceBinding& resource,
		const ShaderReflectionInfo& reflection);

	// bindless indexのuintと対応するTexture SRVが両方ある場合だけテクスチャparamと判定する
	bool IsReflectedTextureParam(const ShaderConstantBufferVariable& variable, const ShaderReflectionInfo& reflection);

	std::string_view GetReflectedTextureDisplayName(const ShaderResourceBinding& resource,
		const ShaderReflectionInfo& reflection);

	MaterialParameterID GetReflectedTextureParameterID(const ShaderResourceBinding& resource,
		const ShaderReflectionInfo& reflection);

	MaterialParameterSemantic GetReflectedTextureSemantic(const ShaderResourceBinding& resource,
		const ShaderReflectionInfo& reflection);

	// 変数タイプから既定のMaterialParameterValueを生成する
	MaterialParameterValue DefaultValueForVariable(const ShaderConstantBufferVariable& var);

	// バリアントからi番目のfloat成分を取り出し、型が違っても安全に変換する
	float ExtractFloatComponent(const MaterialParameterValue& value, int idx);

	// reflectionの型情報とラベルに基づいてUIを表示し、編集結果をValueEditResultで返す
	ValueEditResult DrawValueEdit(const ShaderConstantBufferVariable& var, MaterialParameterValue& value,
		const FloatEditSetting& floatSetting = FloatEditSetting{});

	// 未登録値を補完して指定定数Bufferの編集結果を返す
	ValueEditResult DrawReflectedCBufferParameters(const ShaderReflectionInfo& reflection, const std::string& cbufferName,
		MaterialParameterSet& parameters, std::unordered_set<uint64_t>& drawn);

	ValueEditResult DrawGraphOverrides(MaterialParameterSet& instance, const MaterialParameterSet& defaults,
		std::span<const MaterialParameterID> exposed, const AssetDatabase* database);
} // namespace Engine::MaterialParameterEditor
