---
name: material-param-system
description: reflection-driven material parameter system (CBV) + Mesh per-instance tint; binder wired into Mesh/Sprite/Text additively (no-op for builtins)
metadata:
  type: project
---

2026-06-14 (在席外で実装、build+50s smoke検証のみ、実機GUI未確認). Goal: custom materials whose params/buffer differ per shader, editable + bound generically (like PostProcess auto-reflection). 3層モデル: 共通per-instance(色/UV、既存instance buffer据え置き) / マテリアル共有CBV(reflection駆動) / テクスチャ(material SRV、未実装).

**DONE & verified-no-regression:**
- `Core/Rendering/Materials/MaterialParameterLayout` + `MaterialParameterBufferBuilder` (PostProcessParameter* を昇格・ドメイン非依存化、cbuffer名引数化、既定"MaterialParameters"). PostProcessは"PostProcessParameters"を渡して現状維持. `MaterialAsset.parameters`(name→variant)を既存活用.
- `PipelineState::GetGraphicsReflection()` 追加(全stage統合reflection、CreateGraphicsでMergeShaderReflection).
- `MaterialParameterBinder` (Materials/): pipeline毎にlayoutキャッシュ→pack→CBVアップロード(GPUアドレス返す). MaterialParameters cbuffer無ければ0(no-op). allocはPostProcessConstantBufferAllocatorを流用.
- Mesh/Sprite/Text backendへ配線(slot名"MaterialParameters"、Builtinはこのcbuffer無→slot未解決で何もしない=無回帰). MeshはMeshPreparedBatch.material経由、Sprite/TextはresolvedPass.material経由.
- **Mesh per-instance color(tint)**: `MeshInstanceData`(MeshBatchResources.h)+ HLSL `MeshInstance`(meshShaderSharedTypes.hlsli)に`float4 color`追加(160→176B、RTはMeshInstance未使用で同期不要). MeshRendererComponent.color追加+serialize(未保存時は白維持、空jsonは黒注意)+inspector ColorEdit+ComputeMeshContentHashに含める. 4つのmesh PS(DefaultMesh/Transparent/RayQueryShadow/Vertex)で`baseColor *= gMeshInstances[input.instanceID].color`. **gSubMeshes(MeshSubMeshShaderData)は無変更**(サブメッシュ材質は据え置き、絶対変えない制約).

**追加DONE(2026-06-14、build+smoke検証):**
- Material inspector **auto-reflection**: `PipelineStateCache` に pipelineAsset→統合reflection索引(`FindGraphicsReflection`、描画済みPSOから引く・editorでPSO再生成しない)、`RenderPipelineRunner::FindPipelineGraphicsReflection` で公開. InspectorPanel.DrawMaterialAssetInspector に "Shader Parameters"(MaterialParameters cbufferを最初から編集可能で自動列挙)+ "Custom Parameters"(非reflectionのみ). 描画ロジックは共有 `Editor/UI/Common/MaterialParameterEditor.h`(header-only)へ集約.
- **PostProcessStack も「+」廃止で最初から編集可能**化(未overrideでもdefault値で編集可、編集時にoverride生成). PostProcessStackTool の局所ヘルパ(DrawParameterValueEdit等)を削除し共有 MaterialParameterEditor へ統一(DRY).

**追加DONE(2026-06-14、起動smokeでシェーダーコンパイル確認):**
- **デフォルトmesh描画をPBR→ハーフランバートへ**. 新 `Shaders/Builtin/Mesh/DefaultMeshLambert/` に defaultMeshLambert.PS(影無)+ defaultMeshLambertRayQueryShadow.PS(InlineRayQueryShadow有). 共有 `Mesh/Common/meshLighting.hlsli`(ライトcbuffer/struct/buffer/減衰/tile cluster index/法線=PBR PSから抽出)+ `meshLambert.hlsli`(HalfLambert/点/スポット/baseColor/ローカル集計). color/UV/baseColorTextureは**gSubMeshesから**(制約どおり不変)、per-instance tint乗算、3MRT出力. shader.json×3(MS無影/MS有影/VS有影、VS/AS/MSはGUID共有=複製なし)+ defaultMeshLambert.pipeline(4変種、要InlineRT変種で影自動選択). defaultMesh.materialのDrawパスを新Lambert pipeline(cb1dfc9e9ef2bae9)へrepoint(ZPrepass/Transparentは据置=PBR mainTransparentのまま).
- **既存PBRは meshPBR へ**: defaultMesh.shader.json name→"MeshPBR"、選択用 DefaultMesh/meshPBR.material.json(PBR pipeline参照)を追加.
- **要実機GUI確認**: ハーフランバートの見た目。非影Lambert変種はRT-ON scene では未実行(構造同等).

**追加DONE(2026-06-14、build+smoke、meshPBRの見た目は実機未確認):**
- **半透明デフォルトもLambert化**: DefaultMeshLambert/lambertTransparent.shader(Lambert PSのmainTransparent)+pipeline、defaultMesh.materialのTransparentをそこへrepoint.
- **PBRパラメータをMaterialParameters化(汎用)**: 規約はcbuffer名`MaterialParameters`(mesh側はb5、エンジンは名前解決)+space2テクスチャ。**meshPBR専用でなくどのステージのユーザーシェーダーでも同じ規約で使える**. `meshPBRMaterial.hlsli`にcbuffer(Metallic/Roughness)+space2(gMetallicRoughnessMap/gOcclusionMap)宣言. 4つのPBR PS(defaultMesh/transparent/rayQueryShadow/vertex)がgSubMeshesでなくMaterialParameters/space2を読む(glTF流metallic=factor*tex.b)。specularテクスチャは廃止しF0標準化. meshPBR.materialにMetallic/Roughness既定追加.
- **SubMeshMaterial(component)からPBR削除**: metallic/roughness/metallicRoughnessTexture/specularTexture/occlusionTexture を削除. serialize/hash/inspector/authoring/MeshDrawPathCommon/MeshBatchResources/RaytracingSceneBuilder を修正. **GPU struct(MeshSubMeshShaderData/byte一致)は温存**し未使用フィールドはdefault/モデル既定のまま(byte一致リスク回避). 旧シーンのmetallic等jsonキーは無視され後方互換.

**完全リフレクション型per-submesh化DONE(2026-06-14、build+55s smoke、root paramにgMeshMaterialParameters確認済、実機の見た目は未確認):**
- ユーザー要望: SubMeshMaterialのPS使用param全削除(color/emissiveColor/textures)+MeshRendererComponent.color削除し完全reflection駆動へ. テクスチャもbindless reflection param. UVは優先度低で据置(gSubMeshesのuvMatrixのまま).
- **mesh描画は1 DispatchMeshで全サブメッシュ→cbuffer差し替え不可**. よってサブメッシュ単位の**可変stride構造化バッファ`gMeshMaterialParameters`(t0,space3)**をgSubMeshesと同じinstance×subMesh indexで引く. マテリアル=既定値、サブメッシュ=上書き(Unity風).
- reflection拡張(`DxShaderCompiler` ParseShaderReflection): StructuredBufferの要素structをメンバ展開しcbufferと同じoffset解決. 通常cbufferは不変で無回帰.
- `SubMeshMaterial`: color/emissiveColor/baseColorTexture/normalTexture/emissiveTexture削除→`parameterOverrides`(name→MaterialParameterValue)に集約. 旧json keyはfrom_jsonで同名移行. テクスチャはAssetID値でpack時bindless indexへ解決(空はkNoTexture=UINT32_MAX). model既定テクスチャはMeshSubMeshAuthoringがparameterOverridesへ.
- `MeshRendererComponent.color`(per-instance tint)削除. gSubMeshes/MeshInstanceのcolor/textureIndex fieldはbyte一致温存だが未populate(shaderはparam buffer参照).
- pack: `MaterialParameterBufferBuilder::BuildElement`(既定+上書き+textureResolver). バッファ/SRVは`MeshBatchResources`に可変stride raw構造化バッファとして内蔵(UploadSubMeshMaterialParams、draw時pipeline reflectionでlayout解決). bindは`MeshRenderBackend::BindSharedResources`でgMeshMaterialParameters slot.
- shader: meshLambert.hlsli/meshPBRMaterial.hlsliが各々`struct MeshMaterialParameters`(16整列)+buffer+`GetInstanceMeshMaterialParameters`. Lambert PS×2/PBR PS×4をparam参照に書換, ComputeWorldNormalはnormalTextureIndex引数化, bindless `SamplePBRTexture`. defaultMesh/meshPBR materialにcolor既定追加.
- inspector: MeshRendererInspectorDrawerがマテリアルのDrawパスreflectionをロードしgMeshMaterialParametersメンバを最初から編集可(テクスチャはAssetReferenceField, 数値はDrawValueEdit). materialはAssetIDキャッシュでファイル再読込最小化.
- RT(RaytracingSceneBuilder): fixed SubMeshShaderDataにparameterOverridesから既知名(color/Metallic/Roughness)を詰める. テクスチャはMeshDrawPathCommon resolverがparameterOverrides参照で解決.
- **既知の割り切り**: glTF baseColor factor(importedBaseColor)はbaseColor乗算から外れた(material color既定=白×texture). subMesh色のtimeline animation登録は削除(map memberはメンバポインタ不可). UV reflection化は未実装.

**残(未実装):**
- **テクスチャをmaterial param(reflected SRV + 無し対応)**: 未実装. **要規約決定**=エンジン供給SRV(gPackedVertices/gMeshInstances/gAtlas/gTexture等)とマテリアルテクスチャの区別基準(専用register space案 等). 既存の手動AssetID paramは Custom Parameters で編集は可能だがbindは未対応.
- 任意paramの**per-instance PropertyBlock**: 未実装(色/UVの共通層がその役).
- 補足: PostProcessのun-override表示値は型default(実マテリアル値ではない)、従来踏襲.

関連: [[shader-asset-layout]] [[rt-hlsl-struct-sync]] [[template-comment-style]]
