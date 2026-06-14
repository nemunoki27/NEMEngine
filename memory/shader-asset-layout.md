---
name: shader-asset-layout
description: Builtin shader assets reorganized to 1-hlsl-set-per-folder under Shaders/, all source refs are GUID, resolver is ShaderSourcePathResolver.h
metadata:
  type: project
---

2026-06-14 reorg of `Engine/Assets/`: `.shader.json`/`.pipeline.json`/`.material.json` now live **with their `.hlsl`** under `Shaders/Builtin/<Group>/<ShaderName>/` (1 hlsl set = 1 folder). Top-level `Pipelines/` and `Materials/` folders were **deleted**. AssetDatabase scans recursively so GUID refs (material→pipeline→shader) are path-independent.

Shared hlsl was **duplicated + renamed** per shader (not cross-referenced): `defaultMeshTransparent`, `RayQueryShadow` (was defaultMeshRaytracing — TLAS shadow PS), `RayQueryShadowVertex` (was defaultMeshVertexRaytracing), `toneMapToView.VS` (copy of fullscreenCopy.VS). Common bodies factored into hlsli: `FullscreenTriangleVS` in `FullscreenCopy/fullscreenCopy.hlsli`; `BuildMeshSurfaceVertex` at the **end** of `Mesh/Common/defaultMesh.hlsli` (HLSL needs decl-before-use — it calls LoadMeshVertex which is defined late). AS/MS stay duplicated (mesh-shader groupshared/output semantics can't be moved to a callable).

**All shader-source refs are now GUID** (the hlsl `.meta` guid), not paths: every `shader.json` stage `file`, and the 8 C++ inline `desc.*.file` (SceneGridRenderer, LineRendererBase.h, MeshSubMeshPicker, SceneComponentOverlayRenderer). HLSL `#include` stays path-relative (compiler-level). Resolution is `Core/Rendering/Pipelines/ShaderSourcePathResolver.h` (`Engine::ShaderSourcePath::Resolve`, header-only inline, builds guid→path + filename→path indexes at startup by scanning Shaders/ + reading `.meta`). Both `PipelineState::ResolveShaderPath` and `RaytracingPipelineState` delegate to it. PostProcessAssetGenerator now generates pipeline/material **next to** the shader (co-located), no Pipelines/Materials tree. See [[rt-hlsl-struct-sync]] [[template-comment-style]].

**Mesh shaders renamed+regrouped (2026-06-14, build+smoke, missingReferences=0)**: ハーフランバートがエンジン既定なので "Lambert" 名を廃止。`Mesh/` 配下を2グループへ整理:
- `DefaultMesh/` = 既定(ハーフランバート)+共有geometry: `defaultMesh.VS/AS/MS`(共有), `defaultMesh.PS`(旧defaultMeshLambert.PS), `defaultMeshRayQueryShadow.PS`(旧Lambert影), shader/pipeline(`defaultMesh`/`defaultMeshRayQueryShadow`/`defaultMeshRayQueryShadowVertex`/`defaultMeshTransparent`), `defaultMesh.material`(既定material).
- `MeshPBR/` = PBR一式: `meshPBR.PS`(旧DefaultMesh/defaultMesh.PS), `meshPBRRayQueryShadow.*`(旧RayQueryShadow/), `meshPBRRayQueryShadowVertex.*`, `meshPBRTransparent.*`(旧DefaultMeshTransparent/、独自VS/AS/MS持ち), `meshPBR.material`.
- 削除フォルダ: `DefaultMeshLambert/` `DefaultMeshTransparent/` `RayQueryShadow/` `RayQueryShadowVertex/`. 共有のまま: `DefaultMeshZPrepass/` `DefaultMeshOutline/` `Culling/` `Skinning/` `Common/`.
- GUID参照のため移動はファイル+`.meta`のmvのみで参照不変(shader.jsonのstage GUIDも編集不要)、`name`フィールドのみclarityで更新。`#include "../Common/..."` は全フォルダdepth1維持で有効.
- ProjectPanelで `.hlsli` を非表示化(`ProjectAssetIndex::ShouldHideInBrowser`).
