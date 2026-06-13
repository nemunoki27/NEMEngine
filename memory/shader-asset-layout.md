---
name: shader-asset-layout
description: Builtin shader assets reorganized to 1-hlsl-set-per-folder under Shaders/, all source refs are GUID, resolver is ShaderSourcePathResolver.h
metadata:
  type: project
---

2026-06-14 reorg of `Engine/Assets/`: `.shader.json`/`.pipeline.json`/`.material.json` now live **with their `.hlsl`** under `Shaders/Builtin/<Group>/<ShaderName>/` (1 hlsl set = 1 folder). Top-level `Pipelines/` and `Materials/` folders were **deleted**. AssetDatabase scans recursively so GUID refs (material→pipeline→shader) are path-independent.

Shared hlsl was **duplicated + renamed** per shader (not cross-referenced): `defaultMeshTransparent`, `RayQueryShadow` (was defaultMeshRaytracing — TLAS shadow PS), `RayQueryShadowVertex` (was defaultMeshVertexRaytracing), `toneMapToView.VS` (copy of fullscreenCopy.VS). Common bodies factored into hlsli: `FullscreenTriangleVS` in `FullscreenCopy/fullscreenCopy.hlsli`; `BuildMeshSurfaceVertex` at the **end** of `Mesh/Common/defaultMesh.hlsli` (HLSL needs decl-before-use — it calls LoadMeshVertex which is defined late). AS/MS stay duplicated (mesh-shader groupshared/output semantics can't be moved to a callable).

**All shader-source refs are now GUID** (the hlsl `.meta` guid), not paths: every `shader.json` stage `file`, and the 8 C++ inline `desc.*.file` (SceneGridRenderer, LineRendererBase.h, MeshSubMeshPicker, SceneComponentOverlayRenderer). HLSL `#include` stays path-relative (compiler-level). Resolution is `Core/Rendering/Pipelines/ShaderSourcePathResolver.h` (`Engine::ShaderSourcePath::Resolve`, header-only inline, builds guid→path + filename→path indexes at startup by scanning Shaders/ + reading `.meta`). Both `PipelineState::ResolveShaderPath` and `RaytracingPipelineState` delegate to it. PostProcessAssetGenerator now generates pipeline/material **next to** the shader (co-located), no Pipelines/Materials tree. See [[rt-hlsl-struct-sync]] [[template-comment-style]].
