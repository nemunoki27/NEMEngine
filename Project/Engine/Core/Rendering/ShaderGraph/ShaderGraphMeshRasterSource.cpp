#include "ShaderGraphStageSource.h"

//============================================================================
//	include
//============================================================================

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace Engine::ShaderGraphStageSource {

	// Mesh描画のShaderを生成する
	std::string BuildMeshPixelSource(std::string_view surfaceIncludeFile, bool transparent) {

		std::string source = "// Shader Graph generated file\n"
							 "#define NEM_SHADER_GRAPH_MATERIAL\n";
		// 不透明はGBufferへ透明はLighting結果へ出力する
		if (transparent) {
			source += "#include \"Builtin/Mesh/Common/meshSurfaceLighting.hlsli\"\n";
		} else {
			source += "#include \"Builtin/Mesh/Common/defaultMesh.hlsli\"\n"
					  "#include \"Builtin/Mesh/Common/meshLighting.hlsli\"\n"
					  "#include \"Builtin/Mesh/Common/deferredGBuffer.hlsli\"\n";
		}
		source += "#include \"" + std::string(surfaceIncludeFile) +
				  "\"\n\n"
				  "StructuredBuffer<ShaderGraphParameters> gMeshMaterialParameters : register(t0, space3);\n\n"
				  "ShaderGraphParameters GetShaderGraphParameters(uint instanceID, uint localSubMeshIndex) {\n\n"
				  "\tMeshInstance instance = gMeshInstances[instanceID];\n"
				  "\tuint safeCount = max(instance.subMeshCount, 1u);\n"
				  "\tuint clampedIndex = min(localSubMeshIndex, safeCount - 1u);\n"
				  "\treturn gMeshMaterialParameters[instance.subMeshDataOffset + clampedIndex];\n"
				  "}\n\n"
				  "ShaderGraphSurface EvaluateRasterShaderGraph(VSOutput input) {\n\n"
				  "\tSubMeshShaderData subMesh = GetInstanceSubMesh(input.instanceID, input.subMeshIndex);\n"
				  "\tShaderGraphSurfaceInput graphInput;\n"
				  "\tgraphInput.uv = mul(float4(input.uv, 0.0f, 1.0f), subMesh.uvMatrix).xy;\n"
				  "\tgraphInput.worldNormal = normalize(input.normal);\n"
				  "\tgraphInput.worldPosition = input.worldPos;\n"
				  "\tgraphInput.objectPosition = input.worldPos;\n"
				  "\tgraphInput.objectNormal = input.normal;\n"
				  "\tgraphInput.objectTangent = input.tangent;\n"
				  "\tgraphInput.viewDirection = normalize(renderCameraPos - input.worldPos);\n"
				  "\tgraphInput.screenPosition = input.position;\n"
				  "\tgraphInput.vertexColor = 1.0f.xxxx;\n"
				  "\tgraphInput.tangentToWorld = BuildMeshTBN(input);\n"
				  "\treturn EvaluateShaderGraphSurface(graphInput,\n"
				  "\t\tGetShaderGraphParameters(input.instanceID, input.subMeshIndex));\n"
				  "}\n\n";

		if (!transparent) {
			source += "GBufferOutput main(VSOutput input) {\n\n"
					  "\tApplyMeshLODDither(input.position.xy, input.lodCoverage);\n"
					  "\tShaderGraphSurface graph = EvaluateRasterShaderGraph(input);\n"
					  "\tclip(graph.baseColor.a * graph.opacity - graph.alphaClip);\n"
					  "\tMeshSurface surface;\n"
					  "\tsurface.albedo = graph.baseColor.rgb;\n"
					  "\tsurface.normal = graph.normal;\n"
					  "\tsurface.worldPos = input.worldPos;\n"
					  "\tsurface.metallic = graph.metallic;\n"
					  "\tsurface.roughness = graph.roughness;\n"
					  "\tsurface.occlusion = graph.ambientOcclusion;\n"
					  "\tsurface.emissive = graph.emissive;\n"
					  "\tsurface.motion = ComputeGBufferMotion(input.currentClipPosition, input.previousClipPosition);\n"
					  "\tsurface.flags = BuildMaterialFlags(gMeshInstances[input.instanceID].flags);\n"
					  "\treturn EncodeGBuffer(surface);\n"
					  "}\n";
		} else {
			source += "struct TransparentPSOutput {\n\n"
					  "\tfloat4 color : SV_TARGET0;\n"
					  "};\n\n"
					  "TransparentPSOutput mainTransparent(VSOutput input) {\n\n"
					  "\tApplyMeshLODDither(input.position.xy, input.lodCoverage);\n"
					  "\tShaderGraphSurface graph = EvaluateRasterShaderGraph(input);\n"
					  "\tfloat alpha = graph.baseColor.a * graph.opacity;\n"
					  "\tclip(alpha - graph.alphaClip);\n"
					  "\tResolvedPBRMaterial material;\n"
					  "\tmaterial.baseColor = graph.baseColor;\n"
					  "\tmaterial.N = graph.normal;\n"
					  "\tmaterial.metallic = graph.metallic;\n"
					  "\tmaterial.roughness = graph.roughness;\n"
					  "\tmaterial.ao = graph.ambientOcclusion;\n"
					  "\tmaterial.emissive = graph.emissive;\n"
					  "\tTransparentPSOutput output;\n"
					  "\toutput.color = float4(EvaluateMeshSurfaceLighting(input, material), alpha);\n"
					  "\treturn output;\n"
					  "}\n";
		}
		return source;
	}

	// Mesh描画のShaderを生成する
	std::string BuildMeshAuxiliaryPixelSource(std::string_view surfaceIncludeFile, bool picking) {

		std::string source =
			"// Shader Graph generated Mesh auxiliary file\n"
			"#define NEM_SHADER_GRAPH_MATERIAL\n"
			"#include \"Builtin/Mesh/Common/defaultMesh.hlsli\"\n"
			"SamplerState gSampler : register(s0);\n"
			"#include \"" +
			std::string(surfaceIncludeFile) +
			"\"\n\n"
			"StructuredBuffer<ShaderGraphParameters> gMeshMaterialParameters : register(t0, space3);\n\n"
			"ShaderGraphParameters GetShaderGraphParameters(uint instanceID, uint localSubMeshIndex) {\n\n"
			"\tMeshInstance instance = gMeshInstances[instanceID];\n"
			"\tuint safeCount = max(instance.subMeshCount, 1u);\n"
			"\tuint clampedIndex = min(localSubMeshIndex, safeCount - 1u);\n"
			"\treturn gMeshMaterialParameters[instance.subMeshDataOffset + clampedIndex];\n"
			"}\n\n"
			"ShaderGraphSurface EvaluateRasterShaderGraph(VSOutput input) {\n\n"
			"\tSubMeshShaderData subMesh = GetInstanceSubMesh(input.instanceID, input.subMeshIndex);\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = mul(float4(input.uv, 0.0f, 1.0f), subMesh.uvMatrix).xy;\n"
			"\tgraphInput.worldNormal = normalize(input.normal);\n"
			"\tgraphInput.worldPosition = input.worldPos;\n"
			"\tgraphInput.objectPosition = input.worldPos;\n"
			"\tgraphInput.objectNormal = input.normal;\n"
			"\tgraphInput.objectTangent = input.tangent;\n"
			"\tgraphInput.viewDirection = normalize(renderCameraPos - input.worldPos);\n"
			"\tgraphInput.screenPosition = input.position;\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = BuildMeshTBN(input);\n"
			"\treturn EvaluateShaderGraphSurface(graphInput, GetShaderGraphParameters(input.instanceID, input.subMeshIndex));\n"
			"}\n\n";
		// 同じ透明度評価を選択と影へ適用する
		if (picking) {
			source += "uint4 main(VSOutput input) : SV_Target0 {\n\n"
					  "\tShaderGraphSurface graph = EvaluateRasterShaderGraph(input);\n"
					  "\tclip(graph.baseColor.a * graph.opacity - graph.alphaClip);\n"
					  "\tMeshInstance instance = gMeshInstances[input.instanceID];\n"
					  "\treturn uint4(instance.entityIndex, instance.entityGeneration, input.subMeshIndex, 1u);\n"
					  "}\n";
		} else {
			source += "void main(VSOutput input) {\n\n"
					  "\tApplyMeshLODDither(input.position.xy, input.lodCoverage);\n"
					  "\tShaderGraphSurface graph = EvaluateRasterShaderGraph(input);\n"
					  "\tclip(graph.baseColor.a * graph.opacity - graph.alphaClip);\n"
					  "}\n";
		}
		return source;
	}

	// Mesh描画のShaderを生成する
	static std::string BuildMeshVertexCommonSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context) {

		const ShaderGraphNode* output = context.FindNode(graph.vertexOutputNode);
		if (graph.vertexOutputNode && (!output || output->kind != ShaderGraphNodeKind::VertexOutput)) {
			context.AddDiagnostic(graph.vertexOutputNode, "Vertex出力ノードが見つかりません");
			return {};
		}
		// 頂点出力がないグラフは元の頂点をそのまま使用する
		const ShaderGraphNode defaultOutput{};
		if (!output) {
			output = &defaultOutput;
		}
		const ShaderGraphExpression position =
			context.EmitInput(*output, 0, ShaderGraphValueType::Float3, "vertex.position.xyz");
		const ShaderGraphExpression normal = context.EmitInput(*output, 1, ShaderGraphValueType::Float3, "vertex.normal");
		const ShaderGraphExpression tangent = context.EmitInput(*output, 2, ShaderGraphValueType::Float3, "vertex.tangent");

		std::string source =
			"// Shader Graph generated Mesh vertex file\n"
			"#define NEM_SHADER_GRAPH_MATERIAL\n"
			"#include \"Builtin/Mesh/Common/defaultMesh.hlsli\"\n"
			"SamplerState gSampler : register(s0);\n"
			"#include \"" +
			std::string(surfaceIncludeFile) +
			"\"\n\n"
			"StructuredBuffer<ShaderGraphParameters> gMeshMaterialParameters : register(t0, space3);\n\n"
			"ShaderGraphParameters GetShaderGraphVertexParameters(uint instanceID, uint localSubMeshIndex) {\n\n"
			"\tMeshInstance instance = gMeshInstances[instanceID];\n"
			"\tuint safeCount = max(instance.subMeshCount, 1u);\n"
			"\tuint clampedIndex = min(localSubMeshIndex, safeCount - 1u);\n"
			"\treturn gMeshMaterialParameters[instance.subMeshDataOffset + clampedIndex];\n"
			"}\n\n"
			"struct ShaderGraphVertexResult {\n\n"
			"\tfloat3 position;\n"
			"\tfloat3 normal;\n"
			"\tfloat3 tangent;\n"
			"};\n\n"
			"ShaderGraphVertexResult EvaluateShaderGraphVertex(MeshVertex vertex, uint instanceID, uint localSubMeshIndex, "
			"float4x4 worldMatrix, float4x4 normalMatrix) {\n\n"
			"\tfloat3 originalWorldPosition = mul(vertex.position, worldMatrix).xyz;\n"
			"\tfloat3 originalWorldNormal = TransformMeshNormalToWorld(vertex.normal, normalMatrix);\n"
			"\tfloat3 originalWorldTangent = TransformMeshTangentToWorld(vertex.tangent, worldMatrix);\n"
			"\tShaderGraphSurfaceInput graphInput;\n"
			"\tgraphInput.uv = vertex.uv;\n"
			"\tgraphInput.worldNormal = originalWorldNormal;\n"
			"\tgraphInput.worldPosition = originalWorldPosition;\n"
			"\tgraphInput.objectPosition = vertex.position.xyz;\n"
			"\tgraphInput.objectNormal = vertex.normal;\n"
			"\tgraphInput.objectTangent = vertex.tangent;\n"
			"\tgraphInput.viewDirection = normalize(renderCameraPos - originalWorldPosition);\n"
			"\tgraphInput.screenPosition = mul(float4(originalWorldPosition, 1.0f), viewProjection);\n"
			"\tgraphInput.vertexColor = 1.0f.xxxx;\n"
			"\tgraphInput.tangentToWorld = float3x3(originalWorldTangent, cross(originalWorldNormal, originalWorldTangent) * "
			"vertex.tangentSign, originalWorldNormal);\n"
			"\tShaderGraphParameters graphParameters = GetShaderGraphVertexParameters(instanceID, localSubMeshIndex);\n"
			"\tShaderGraphVertexResult result;\n";
		source += context.GetEvaluationStatements();
		source += "\tresult.position = " + position.code + ";\n";
		source += "\tresult.normal = normalize(" + normal.code + ");\n";
		source += "\tresult.tangent = normalize(" + tangent.code + ");\n";
		source += "\treturn result;\n"
				  "}\n\n";
		return source;
	}

	// Mesh描画のShaderを生成する
	std::string BuildMeshVertexSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context) {

		// 共通の頂点評価を通常の頂点経路へ接続する
		std::string source = BuildMeshVertexCommonSource(graph, surfaceIncludeFile, context);
		source += "VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {\n\n"
				  "\tMeshVertex vertex = LoadMeshVertex(instanceID, vertexID);\n"
				  "\tuint localSubMeshIndex = gVertexSubMeshIndices[vertexID];\n"
				  "\tfloat4x4 worldMatrix = GetInstanceSubMeshWorldMatrix(instanceID, localSubMeshIndex);\n"
				  "\tfloat4x4 normalMatrix = GetInstanceSubMeshNormalMatrix(instanceID, localSubMeshIndex);\n"
				  "\tShaderGraphVertexResult graph = EvaluateShaderGraphVertex(vertex, instanceID, localSubMeshIndex, "
				  "worldMatrix, normalMatrix);\n"
				  "\tfloat4 worldPosition = mul(float4(graph.position, 1.0f), worldMatrix);\n"
				  "\tVSOutput output;\n"
				  "\toutput.position = mul(worldPosition, viewProjection);\n"
				  "\toutput.currentClipPosition = output.position;\n"
				  "\tfloat4 previousPosition = mul(float4(graph.position, 1.0f), "
				  "GetInstanceSubMeshPreviousWorldMatrix(instanceID, localSubMeshIndex));\n"
				  "\toutput.previousClipPosition = (gMeshInstances[instanceID].flags & MESH_INSTANCE_FLAG_SKINNED) != 0u ? "
				  "output.position : mul(previousPosition, previousViewProjection);\n"
				  "\toutput.worldPos = worldPosition.xyz;\n"
				  "\toutput.normal = TransformMeshNormalToWorld(graph.normal, normalMatrix);\n"
				  "\toutput.tangent = TransformMeshTangentToWorld(graph.tangent, worldMatrix);\n"
				  "\toutput.uv = vertex.uv;\n"
				  "\toutput.instanceID = instanceID;\n"
				  "\toutput.subMeshIndex = localSubMeshIndex;\n"
				  "\toutput.tangentSign = vertex.tangentSign;\n"
				  "\toutput.orientationSign = GetInstanceSubMeshOrientationSign(instanceID, localSubMeshIndex);\n"
				  "\toutput.lodCoverage = GetMeshInstanceLODCoverage(instanceID);\n"
				  "\tApplyMeshRenderGroupVisibility(output);\n"
				  "\treturn output;\n"
				  "}\n";
		return source;
	}

	// Mesh描画のShaderを生成する
	std::string BuildMeshShaderSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context) {

		std::string source = BuildMeshVertexCommonSource(graph, surfaceIncludeFile, context);
		// Meshlet内で変換行列を共有する
		source += "groupshared float4x4 gGraphWorldMatrix;\n"
				  "groupshared float4x4 gGraphNormalMatrix;\n"
				  "groupshared float gGraphOrientationSign;\n\n"
				  "[outputtopology(\"triangle\")]\n"
				  "[numthreads(128, 1, 1)]\n"
				  "void main(uint groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID, in payload MeshDispatchPayload "
				  "payload, out vertices VSOutput outVerts[64], out indices uint3 outTris[124]) {\n\n"
				  "\tuint meshletIndex = payload.meshletIndices[groupID.x];\n"
				  "\tuint instanceIndex = payload.instanceIndices[groupID.x];\n"
				  "\tMeshletDrawDesc meshlet = gMeshlets[meshletIndex];\n"
				  "\tSetMeshOutputCounts(meshlet.vertexCount, meshlet.primitiveCount);\n"
				  "\tuint localSubMeshIndex = meshlet.subMeshIndex;\n"
				  "\tif (groupThreadID == 0) {\n"
				  "\t\tgGraphWorldMatrix = GetInstanceSubMeshWorldMatrix(instanceIndex, localSubMeshIndex);\n"
				  "\t\tgGraphNormalMatrix = GetInstanceSubMeshNormalMatrix(instanceIndex, localSubMeshIndex);\n"
				  "\t\tgGraphOrientationSign = GetInstanceSubMeshOrientationSign(instanceIndex, localSubMeshIndex);\n"
				  "\t}\n"
				  "\tGroupMemoryBarrierWithGroupSync();\n"
				  "\tif (groupThreadID < meshlet.primitiveCount) outTris[groupThreadID] = "
				  "UnpackPrimitiveIndex(gMeshletPrimitiveIndices[meshlet.primitiveOffset + groupThreadID]);\n"
				  "\tif (groupThreadID < meshlet.vertexCount) {\n"
				  "\t\tuint vertexIndex = LoadMeshletVertexIndex(meshlet.vertexOffset + groupThreadID);\n"
				  "\t\tMeshVertex vertex = LoadMeshVertex(instanceIndex, vertexIndex);\n"
				  "\t\tShaderGraphVertexResult graph = EvaluateShaderGraphVertex(vertex, instanceIndex, localSubMeshIndex, "
				  "gGraphWorldMatrix, gGraphNormalMatrix);\n"
				  "\t\tfloat4 worldPosition = mul(float4(graph.position, 1.0f), gGraphWorldMatrix);\n"
				  "\t\tVSOutput output;\n"
				  "\t\toutput.position = mul(worldPosition, viewProjection);\n"
				  "\t\toutput.currentClipPosition = output.position;\n"
				  "\t\tfloat4 previousPosition = mul(float4(graph.position, 1.0f), "
				  "GetInstanceSubMeshPreviousWorldMatrix(instanceIndex, localSubMeshIndex));\n"
				  "\t\toutput.previousClipPosition = (gMeshInstances[instanceIndex].flags & MESH_INSTANCE_FLAG_SKINNED) != 0u "
				  "? output.position : mul(previousPosition, previousViewProjection);\n"
				  "\t\toutput.worldPos = worldPosition.xyz;\n"
				  "\t\toutput.normal = TransformMeshNormalToWorld(graph.normal, gGraphNormalMatrix);\n"
				  "\t\toutput.tangent = TransformMeshTangentToWorld(graph.tangent, gGraphWorldMatrix);\n"
				  "\t\toutput.uv = vertex.uv;\n"
				  "\t\toutput.instanceID = instanceIndex;\n"
				  "\t\toutput.subMeshIndex = localSubMeshIndex;\n"
				  "\t\toutput.tangentSign = vertex.tangentSign;\n"
				  "\t\toutput.orientationSign = gGraphOrientationSign;\n"
				  "\t\toutput.lodCoverage = payload.lodCoverages[groupID.x];\n"
				  "\t\toutVerts[groupThreadID] = output;\n"
				  "\t}\n"
				  "}\n";
		return source;
	}

} // Engine::ShaderGraphStageSource
