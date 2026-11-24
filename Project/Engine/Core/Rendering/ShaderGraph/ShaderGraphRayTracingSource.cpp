#include "ShaderGraphStageSource.h"
#include "ShaderGraphRuntimeInputSource.h"

//============================================================================
//	include
//============================================================================

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace Engine::ShaderGraphStageSource {

	// RayTracingのShaderを生成する
	std::string BuildRayTracingSource(std::string_view surfaceIncludeFile) {

		std::string source = "// Shader Graph generated Ray Tracing library\n"
							 "#define NEM_REFLECTION_CUSTOM_HIT\n"
							 "#define NEM_SHADER_GRAPH_EXPLICIT_LOD 1\n"
							 "#include \"Builtin/Raytracing/reflection.RT.hlsl\"\n"
							 "#include \"" +
							 std::string(surfaceIncludeFile) + "\"\n\n";
		// 命中面の属性を表面評価へ渡す
		source += "ShaderGraphSurface EvaluateRayTracingShaderGraph(out SubMeshShaderData subMesh,\n"
				  "\tin BuiltInTriangleIntersectionAttributes attr) {\n\n"
				  "\tRaytracingInstanceShaderData instanceData =\n"
				  "\t\tgRaytracingSceneInstances[InstanceID()];\n"
				  "\tRaytracingGeometryShaderData geometryData =\n"
				  "\t\tgRaytracingGeometries[instanceData.geometryDataOffset + GeometryIndex()];\n"
				  "\tsubMesh =\n"
				  "\t\tgRaytracingSubMeshes[geometryData.subMeshDataIndex];\n"
				  "\tStructuredBuffer<uint> indices =\n"
				  "\t\tResourceDescriptorHeap[NonUniformResourceIndex(instanceData.indexDescriptorIndex)];\n"
				  "\tStructuredBuffer<MeshVertex> vertices =\n"
				  "\t\tResourceDescriptorHeap[NonUniformResourceIndex(instanceData.vertexDescriptorIndex)];\n"
				  "\tuint baseIndex = geometryData.indexOffset + PrimitiveIndex() * 3u;\n"
				  "\tMeshVertex v0 = vertices[instanceData.vertexOffset + indices[baseIndex + 0u]];\n"
				  "\tMeshVertex v1 = vertices[instanceData.vertexOffset + indices[baseIndex + 1u]];\n"
				  "\tMeshVertex v2 = vertices[instanceData.vertexOffset + indices[baseIndex + 2u]];\n"
				  "\tfloat3 bary = ComputeBarycentrics(attr.barycentrics);\n"
				  "\tfloat2 uv = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;\n"
				  "\tfloat3 objectPosition = v0.position.xyz * bary.x +\n"
				  "\t\tv1.position.xyz * bary.y + v2.position.xyz * bary.z;\n"
				  "\tfloat3 objectNormal = normalize(v0.normal * bary.x +\n"
				  "\t\tv1.normal * bary.y + v2.normal * bary.z);\n"
				  "\tfloat3 objectTangent = normalize(v0.tangent * bary.x +\n"
				  "\t\tv1.tangent * bary.y + v2.tangent * bary.z);\n"
				  "\tfloat tangentSign = v0.tangentSign * bary.x +\n"
				  "\t\tv1.tangentSign * bary.y + v2.tangentSign * bary.z < 0.0f ? -1.0f : 1.0f;\n"
				  "\tfloat3 localNormal = normalize(mul(float4(objectNormal, 0.0f),\n"
				  "\t\tsubMesh.localNormalMatrix).xyz);\n"
				  "\tfloat3 localTangent = normalize(mul(float4(objectTangent, 0.0f),\n"
				  "\t\tsubMesh.localMatrix).xyz);\n"
				  "\tfloat3x3 objectToWorld = (float3x3)ObjectToWorld3x4();\n"
				  "\tfloat3x3 worldToObject = (float3x3)WorldToObject3x4();\n"
				  "\tfloat3 worldNormal = normalize(mul(localNormal, worldToObject));\n"
				  "\tfloat3 worldTangent = normalize(mul(\n"
				  "\t\tobjectToWorld, localTangent));\n"
				  "\tworldTangent = normalize(worldTangent -\n"
				  "\t\tworldNormal * dot(worldNormal, worldTangent));\n"
				  "\tfloat worldOrientationSign = determinant(objectToWorld) < 0.0f ? -1.0f : 1.0f;\n"
				  "\tfloat bitangentSign = tangentSign *\n"
				  "\t\tsubMesh.localOrientationSign * worldOrientationSign;\n"
				  "\tfloat3 worldBitangent = normalize(\n"
				  "\t\tcross(worldNormal, worldTangent)) * bitangentSign;\n"
				  "\tif (dot(worldNormal, WorldRayDirection()) > 0.0f) {\n"
				  "\t\tworldNormal = -worldNormal;\n"
				  "\t\tworldBitangent = -worldBitangent;\n"
				  "\t}\n"
				  "\tShaderGraphSurfaceInput graphInput = (ShaderGraphSurfaceInput)0;\n" +
			BuildFrameInputAssignments() +
				  "\tfloat4 vertexColor;\n\tResolveHitPrimitiveSurface(subMesh, v0, v1, v2, bary, uv, graphInput.uv, vertexColor);\n"
				  "\tgraphInput.hexTiling = subMesh.hexTiling;\n"
				  "\tgraphInput.worldNormal = worldNormal;\n"
				  "\tgraphInput.worldPosition = WorldRayOrigin() +\n"
				  "\t\tWorldRayDirection() * RayTCurrent();\n"
				  "\tgraphInput.objectPosition = objectPosition;\n"
				  "\tgraphInput.objectNormal = objectNormal;\n"
				  "\tgraphInput.objectTangent = objectTangent;\n"
				  "\tgraphInput.viewDirection = normalize(-WorldRayDirection());\n"
				  "\tfloat4 hitClip = mul(float4(graphInput.worldPosition, 1.0f), shaderGraphWorldToClip);\n"
				  "\tfloat3 ndc = hitClip.xyz / (abs(hitClip.w) > 1e-6f ? hitClip.w : 1e-6f);\n"
				  "\tgraphInput.screenPosition = float4(shaderGraphViewport.xy + (ndc.xy * float2(0.5f, -0.5f) + 0.5f) * shaderGraphViewport.zw, ndc.z, 1.0f);\n"
				  "\tgraphInput.vertexColor = 1.0f.xxxx;\n"
				  "\tgraphInput.tangentToWorld = float3x3(\n"
				  "\t\tworldTangent, worldBitangent, worldNormal);\n"
				  "\tfloat4x3 instanceWorld = ObjectToWorld4x3();\n"
				  "\tfloat4x4 graphWorld = float4x4(float4(instanceWorld[0], 0), float4(instanceWorld[1], 0),\n"
				  "\t\tfloat4(instanceWorld[2], 0), float4(instanceWorld[3], 1));\n" +
				  BuildObjectInputAssignments("mul(subMesh.localMatrix, graphWorld)", "0.0f.xxx", "0.0f.xxx") +
				  BuildSphereBoundsAssignments("geometryData.boundsSphere.xyz", "geometryData.boundsSphere.w", "graphWorld") +
				  "\tgraphInput.isFrontFace = HitKind() == HIT_KIND_TRIANGLE_FRONT_FACE;\n"
				  "\tShaderGraphSurface graph = EvaluateShaderGraphSurface(\n"
				  "\t\tgraphInput, gRaytracingMaterialParameters.Load<ShaderGraphParameters>(geometryData.parameterOffset));\n\tgraph.baseColor *= vertexColor;\n\treturn graph;\n"
				  "}\n\n"
				  "[shader(\"anyhit\")]\n"
				  "void ReflectionAnyHit(inout ReflectionPayload payload,\n"
				  "\tin BuiltInTriangleIntersectionAttributes attr) {\n\n"
				  "\tSubMeshShaderData subMesh;\n"
				  "\tShaderGraphSurface graph = EvaluateRayTracingShaderGraph(subMesh, attr);\n"
				  "\tif (graph.baseColor.a * graph.opacity < graph.alphaClip) {\n"
				  "\t\tIgnoreHit();\n"
				  "\t}\n"
				  "}\n\n"
				  "[shader(\"closesthit\")]\n"
				  "void ReflectionClosestHit(inout ReflectionPayload payload,\n"
				  "\tin BuiltInTriangleIntersectionAttributes attr) {\n\n"
				  "\tSubMeshShaderData subMesh;\n"
				  "\tShaderGraphSurface graph = EvaluateRayTracingShaderGraph(subMesh, attr);\n"
				  "\tRaytracingInstanceShaderData instanceData =\n"
				  "\t\tgRaytracingSceneInstances[InstanceID()];\n"
				  "\tfloat3 worldPosition = WorldRayOrigin() +\n"
				  "\t\tWorldRayDirection() * RayTCurrent();\n"
				  "\tResolvedPBRMaterial material;\n"
				  "\tmaterial.baseColor = graph.baseColor;\n"
				  "\tmaterial.N = graph.normal;\n"
				  "\tmaterial.metallic = graph.metallic;\n"
				  "\tmaterial.roughness = graph.roughness;\n"
				  "\tmaterial.ao = graph.ambientOcclusion;\n"
				  "\tmaterial.emissive = graph.emissive;\n"
				  "\tpayload.hit = 1u;\n"
				  "\tpayload.color = EvaluateRaytracingSurfaceLighting(\n"
				  "\t\tworldPosition, material, instanceData.renderFlags, (subMesh._materialPad >> 16u) == 3u ? graph.transmission : 0.0f);\n"
				  "\tif ((subMesh._materialPad >> 16u) == 3u) {\n"
				  "\t\tpayload.color = ResolveReflectionTransparency(payload.color, graph.baseColor.a * graph.opacity,\n"
				  "\t\t\tgraph.transmission, graph.transmissionColor, graph.refraction, graph.normal, worldPosition,\n"
				  "\t\t\tsubMesh.renderGroupIndex, payload.transmissionDepth);\n"
				  "\t}\n"
				  "\tpayload.worldPosition = worldPosition;\n"
				  "\tpayload.hitDistance = RayTCurrent();\n"
				  "\tpayload.worldNormal = graph.normal;\n"
				  "}\n";
		return source;
	}

	// RayTracingのShaderを生成する
	std::string BuildRayTracingEffectSource(const ShaderGraphAsset& graph, ShaderGraphExpressionCompiler& context) {

		const ShaderGraphNode* output = context.FindNode(graph.outputNode);
		if (!output || output->kind != ShaderGraphNodeKind::RayTracingOutput) {
			context.AddDiagnostic(graph.outputNode, "Ray Tracing Effect出力ノードが見つかりません");
			return {};
		}
		const ShaderGraphExpression color = context.EmitInput(
			*output, 0, ShaderGraphValueType::Float4, "gSourceColor.SampleLevel(gSampler, graphInput.uv, 0.0f)");

		std::string source = "// Shader Graph generated RayTracing Feature\n"
			"#define NEM_REFLECTION_CUSTOM_RAY_GENERATION\n"
			"#define NEM_SHADER_GRAPH_EXPLICIT_LOD 1\n"
			"#include \"Builtin/Raytracing/reflection.RT.hlsl\"\n"
			"#define gShaderGraphSceneColor gSourceColor\n"
			"#define gShaderGraphSceneDepth gSourceDepth\n"
			"#define gShaderGraphSceneNormal gSourceNormal\n"
			"#define gShaderGraphScenePosition gSourcePosition\n"
			"#define gShaderGraphSceneMaterial gSourceMaterial\n"
			"#define gShaderGraphSceneFlags gSourceFlags\n"
			"Texture2D<float4> gShaderGraphSceneEmissive : register(t17);\n";
		source += BuildCommonSource(graph, context, true, false);
		source += context.BuildMaterialConstantBuffer(5, "RayTracingParameters");
		source += context.BuildMaterialParameterGetter();
		source += "struct ShaderGraphRayResult {\n"
			"\tfloat3 color;\n\tfloat hit;\n\tfloat distance;\n\tfloat3 position;\n\tfloat3 normal;\n};\n"
			"ShaderGraphRayResult ShaderGraphTraceScene(float3 origin, float3 direction, float minDistance, float maxDistance, uint mask) {\n"
			"\tRayDesc ray;\n"
			"\tray.Origin = origin;\n"
			"\tray.Direction = SafeNormalize(direction, float3(0.0f, 0.0f, 1.0f));\n"
			"\tray.TMin = max(minDistance, 0.0001f);\n"
			"\tray.TMax = max(maxDistance, ray.TMin);\n"
			"\tReflectionPayload payload = (ReflectionPayload)0;\n"
			"\tTraceRay(gSceneTLAS, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, mask, 0, 1, 0, ray, payload);\n"
			"\tShaderGraphRayResult result;\n"
			"\tresult.color = payload.hit != 0u ? payload.color : EvaluateReflectionEnvironment(ray.Direction, 0.0f);\n"
			"\tresult.hit = payload.hit != 0u ? 1.0f : 0.0f;\n"
			"\tresult.distance = payload.hitDistance;\n"
			"\tresult.position = payload.worldPosition;\n"
			"\tresult.normal = payload.worldNormal;\n"
			"\treturn result;\n}\n"
			"[shader(\"raygeneration\")]\n"
			"void RenderFeatureRayGeneration() {\n"
			"\tuint2 pixel = DispatchRaysIndex().xy;\n"
			"\tuint2 dim = DispatchRaysDimensions().xy;\n"
			"\tif (any(pixel >= dim)) return;\n"
			"\tShaderGraphSurfaceInput graphInput = (ShaderGraphSurfaceInput)0;\n" +
			BuildFrameInputAssignments() +
			"\tgraphInput.screenSize = float2(dim);\n"
			"\tgraphInput.uv = (float2(pixel) + 0.5f) / float2(dim);\n"
			"\tgraphInput.worldPosition = gSourcePosition.Load(int3(pixel, 0)).xyz;\n"
			"\tgraphInput.worldNormal = ShaderGraphDecodeSceneNormal(gSourceNormal.Load(int3(pixel, 0)).xyz);\n"
			"\tgraphInput.objectPosition = graphInput.worldPosition;\n"
			"\tgraphInput.objectNormal = graphInput.worldNormal;\n"
			"\tgraphInput.objectTangent = float3(1.0f, 0.0f, 0.0f);\n"
			"\tgraphInput.viewDirection = SafeNormalize(graphInput.cameraPosition - graphInput.worldPosition, float3(0,0,1));\n"
			"\tgraphInput.screenPosition = float4(float2(pixel) + 0.5f, 0.0f, 1.0f);\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.isFrontFace = true;\n"
			"\tgraphInput.tangentToWorld = float3x3(1,0,0,0,1,0,0,0,1);\n" +
			BuildObjectInputAssignments("float4x4(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)",
				"graphInput.worldPosition", "graphInput.worldPosition") +
			"\tShaderGraphParameters graphParameters = GetShaderGraphParameters();\n";
		source += context.GetEvaluationStatements();
		source += "\tgDestColor[pixel] = " + color.code + ";\n}\n";
		return source;
	}
} // Engine::ShaderGraphStageSource
