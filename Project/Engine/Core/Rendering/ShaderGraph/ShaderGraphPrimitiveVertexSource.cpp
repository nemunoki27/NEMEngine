#include "ShaderGraphStageSource.h"

//============================================================================
//	include
//============================================================================

using namespace Engine;
using namespace Engine::ShaderGraphSourceUtility;

namespace Engine::ShaderGraphStageSource {

	// 形状の頂点のShaderを生成する
	static std::string BuildPrimitiveVertexCommonSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context) {

		const ShaderGraphNode* output = context.FindNode(graph.vertexOutputNode);
		if (!output || output->kind != ShaderGraphNodeKind::VertexOutput) {
			context.AddDiagnostic(graph.vertexOutputNode, "Vertex出力ノードが見つかりません");
			return {};
		}
		// 形状の頂点入力から変形結果を生成する
		const ShaderGraphExpression position =
			context.EmitInput(*output, 0, ShaderGraphValueType::Float3, "vertex.position.xyz");
		const ShaderGraphExpression normal = context.EmitInput(*output, 1, ShaderGraphValueType::Float3, "vertex.normal");
		const ShaderGraphExpression tangent = context.EmitInput(*output, 2, ShaderGraphValueType::Float3, "vertex.tangent");

		std::string source = "// Shader Graph generated Primitive vertex file\n"
							 "#include \"Builtin/Primitive/primitive.hlsli\"\n"
							 "#include \"Builtin/Mesh/Common/meshShaderSharedTypes.hlsli\"\n"
							 "SamplerState gSampler : register(s0);\n"
							 "#include \"" +
							 std::string(surfaceIncludeFile) +
							 "\"\n\n"
							 "StructuredBuffer<MeshVertex> gVertices : register(t0);\n"
							 "StructuredBuffer<PrimitiveInstance> gInstances : register(t1);\n\n";
		source += context.BuildMaterialConstantBuffer();
		source += "\n" + context.BuildMaterialParameterGetter();
		source += "\nstruct ShaderGraphPrimitiveVertexResult {\n\n"
				  "\tfloat3 position;\n"
				  "\tfloat3 normal;\n"
				  "\tfloat3 tangent;\n"
				  "};\n\n"
				  "ShaderGraphPrimitiveVertexResult EvaluatePrimitiveShaderGraphVertex(MeshVertex vertex, PrimitiveInstance "
				  "instance) {\n\n"
				  "\tfloat4 originalWorldPosition = mul(float4(vertex.position.xyz, 1.0f), instance.worldMatrix);\n"
				  "\tfloat3 originalWorldNormal = normalize(mul(vertex.normal, (float3x3)instance.worldMatrix));\n"
				  "\tfloat3 originalWorldTangent = normalize(mul(vertex.tangent, (float3x3)instance.worldMatrix));\n"
				  "\tfloat4 vertexColor = ResolvePrimitiveVertexColor(vertex.position.xyz, instance);\n"
				  "\tShaderGraphSurfaceInput graphInput;\n"
				  "\tgraphInput.uv = mul(float4(vertex.uv, 0.0f, 1.0f), instance.uvMatrix).xy;\n"
				  "\tgraphInput.worldNormal = originalWorldNormal;\n"
				  "\tgraphInput.worldPosition = originalWorldPosition.xyz;\n"
				  "\tgraphInput.objectPosition = vertex.position.xyz;\n"
				  "\tgraphInput.objectNormal = vertex.normal;\n"
				  "\tgraphInput.objectTangent = vertex.tangent;\n"
				  "\tgraphInput.viewDirection = normalize(cameraPosition - originalWorldPosition.xyz);\n"
				  "\tgraphInput.screenPosition = mul(originalWorldPosition, viewProjection);\n"
				  "\tgraphInput.vertexColor = vertexColor;\n"
				  "\tgraphInput.tangentToWorld = float3x3(originalWorldTangent, cross(originalWorldNormal, "
				  "originalWorldTangent) * vertex.tangentSign, originalWorldNormal);\n"
				  "\tShaderGraphParameters graphParameters = GetShaderGraphParameters();\n"
				  "\tShaderGraphPrimitiveVertexResult result;\n";
		source += context.GetEvaluationStatements();
		source += "\tresult.position = " + position.code + ";\n";
		source += "\tresult.normal = normalize(" + normal.code + ");\n";
		source += "\tresult.tangent = normalize(" + tangent.code + ");\n";
		source += "\treturn result;\n"
				  "}\n\n";
		return source;
	}

	// 形状の頂点のShaderを生成する
	std::string BuildPrimitiveVertexSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context) {

		std::string source = BuildPrimitiveVertexCommonSource(graph, surfaceIncludeFile, context);
		source += "VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {\n\n"
				  "\tMeshVertex vertex = gVertices[vertexID];\n"
				  "\tPrimitiveInstance instance = gInstances[instanceID];\n"
				  "\tShaderGraphPrimitiveVertexResult graph = EvaluatePrimitiveShaderGraphVertex(vertex, instance);\n"
				  "\tVSOutput output = BuildPrimitiveVertexOutput(\n"
				  "\t\tgraph.position, graph.normal, graph.tangent,\n"
				  "\t\tvertex.tangentSign, vertex.uv,\n"
				  "\t\tResolvePrimitiveVertexColor(vertex.position.xyz, instance),\n"
				  "\t\tinstance);\n"
				  "\toutput.uvCoordinates.xy = vertex.position.xy;\n"
				  "\treturn output;\n"
				  "}\n";
		return source;
	}

	// 形状の頂点のShaderを生成する
	std::string BuildPrimitiveMeshShaderSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context) {

		std::string source = BuildPrimitiveVertexCommonSource(graph, surfaceIncludeFile, context);
		// Mesh経路も同じ頂点評価を使用する
		source += "cbuffer PrimitiveMeshConstants : register(b1) {\n\n"
				  "\tuint indexCount;\n"
				  "\tuint3 _pad;\n"
				  "};\n"
				  "StructuredBuffer<uint> gIndices : register(t2);\n\n"
				  "#define PRIMITIVE_GROUP_TRIANGLES 32\n\n"
				  "[numthreads(PRIMITIVE_GROUP_TRIANGLES, 1, 1)]\n"
				  "[outputtopology(\"triangle\")]\n"
				  "void main(uint groupThreadID : SV_GroupThreadID, uint3 groupID : SV_GroupID, out vertices VSOutput "
				  "verts[PRIMITIVE_GROUP_TRIANGLES * 3], out indices uint3 tris[PRIMITIVE_GROUP_TRIANGLES]) {\n\n"
				  "\tconst uint totalTriangles = indexCount / 3u;\n"
				  "\tconst uint triangleBase = groupID.x * PRIMITIVE_GROUP_TRIANGLES;\n"
				  "\tconst uint triangleCount = triangleBase < totalTriangles ? min((uint)PRIMITIVE_GROUP_TRIANGLES, "
				  "totalTriangles - triangleBase) : 0u;\n"
				  "\tSetMeshOutputCounts(triangleCount * 3u, triangleCount);\n"
				  "\tif (groupThreadID >= triangleCount) return;\n"
				  "\tPrimitiveInstance instance = gInstances[groupID.y];\n"
				  "\tconst uint indexBase = (triangleBase + groupThreadID) * 3u;\n"
				  "\tfor (uint index = 0; index < 3u; ++index) {\n\n"
				  "\t\tMeshVertex vertex = gVertices[gIndices[indexBase + index]];\n"
				  "\t\tShaderGraphPrimitiveVertexResult graph = EvaluatePrimitiveShaderGraphVertex(vertex, instance);\n"
				  "\t\tverts[groupThreadID * 3u + index] = BuildPrimitiveVertexOutput(\n"
				  "\t\t\tgraph.position, graph.normal, graph.tangent,\n"
				  "\t\t\tvertex.tangentSign, vertex.uv,\n"
				  "\t\t\tResolvePrimitiveVertexColor(vertex.position.xyz, instance),\n"
				  "\t\t\tinstance);\n"
				  "\t\tverts[groupThreadID * 3u + index].uvCoordinates.xy = vertex.position.xy;\n"
				  "\t}\n"
				  "\ttris[groupThreadID] = uint3(groupThreadID * 3u, groupThreadID * 3u + 1u, groupThreadID * 3u + 2u);\n"
				  "}\n";
		return source;
	}

	// 形状の頂点のShaderを生成する
	std::string BuildPrimitive2DVertexSource(
		const ShaderGraphAsset& graph, std::string_view surfaceIncludeFile, ShaderGraphExpressionCompiler& context) {

		const ShaderGraphNode* output = context.FindNode(graph.vertexOutputNode);
		if (!output || output->kind != ShaderGraphNodeKind::VertexOutput) {
			context.AddDiagnostic(graph.vertexOutputNode, "Vertex出力ノードが見つかりません");
			return {};
		}
		const ShaderGraphExpression position =
			context.EmitInput(*output, 0, ShaderGraphValueType::Float3, "vertex.position.xyz");
		const ShaderGraphExpression normal = context.EmitInput(*output, 1, ShaderGraphValueType::Float3, "vertex.normal");
		const ShaderGraphExpression tangent = context.EmitInput(*output, 2, ShaderGraphValueType::Float3, "vertex.tangent");

		// 2DのUV変換と頂点色を入力へ渡す
		std::string source = "// Shader Graph generated Primitive2D vertex file\n"
							 "#include \"Builtin/Primitive/primitive2D.hlsli\"\n"
							 "#include \"Builtin/Mesh/Common/meshShaderSharedTypes.hlsli\"\n"
							 "SamplerState gSampler : register(s0);\n"
							 "#include \"" +
							 std::string(surfaceIncludeFile) +
							 "\"\n\n"
							 "StructuredBuffer<MeshVertex> gVertices : register(t0);\n"
							 "StructuredBuffer<PrimitiveInstance> gInstances : register(t1);\n\n";
		source += context.BuildMaterialConstantBuffer();
		source += "\n" + context.BuildMaterialParameterGetter();
		source += "\nstruct ShaderGraphPrimitive2DVertexResult {\n\n"
				  "\tfloat3 position;\n"
				  "\tfloat3 normal;\n"
				  "\tfloat3 tangent;\n"
				  "};\n\n"
				  "ShaderGraphPrimitive2DVertexResult EvaluatePrimitive2DShaderGraphVertex(MeshVertex vertex, "
				  "PrimitiveInstance instance) {\n\n"
				  "\tfloat4 originalWorldPosition = mul(float4(vertex.position.xyz, 1.0f), instance.worldMatrix);\n"
				  "\tfloat3 originalWorldNormal = normalize(mul(vertex.normal, (float3x3)instance.worldMatrix));\n"
				  "\tfloat3 originalWorldTangent = normalize(mul(vertex.tangent, (float3x3)instance.worldMatrix));\n"
				  "\tShaderGraphSurfaceInput graphInput;\n"
				  "\tfloat2 localTexcoord = ResolvePrimitive2DTexcoord(vertex.uv, instance);\n"
				  "\tgraphInput.uv = mul(float4(localTexcoord, 0.0f, 1.0f), instance.uvMatrix).xy;\n"
				  "\tgraphInput.worldNormal = originalWorldNormal;\n"
				  "\tgraphInput.worldPosition = originalWorldPosition.xyz;\n"
				  "\tgraphInput.objectPosition = vertex.position.xyz;\n"
				  "\tgraphInput.objectNormal = vertex.normal;\n"
				  "\tgraphInput.objectTangent = vertex.tangent;\n"
				  "\tgraphInput.viewDirection = normalize(cameraPosition - originalWorldPosition.xyz);\n"
				  "\tgraphInput.screenPosition = mul(originalWorldPosition, viewProjection);\n"
				  "\tgraphInput.vertexColor = 1.0f.xxxx;\n"
				  "\tgraphInput.tangentToWorld = float3x3(originalWorldTangent, cross(originalWorldNormal, "
				  "originalWorldTangent) * vertex.tangentSign, originalWorldNormal);\n"
				  "\tShaderGraphParameters graphParameters = GetShaderGraphParameters();\n"
				  "\tShaderGraphPrimitive2DVertexResult result;\n";
		source += context.GetEvaluationStatements();
		source += "\tresult.position = " + position.code + ";\n";
		source += "\tresult.normal = normalize(" + normal.code + ");\n";
		source += "\tresult.tangent = normalize(" + tangent.code + ");\n";
		source += "\treturn result;\n"
				  "}\n\n"
				  "VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {\n\n"
				  "\tMeshVertex vertex = gVertices[vertexID];\n"
				  "\tPrimitiveInstance instance = gInstances[instanceID];\n"
				  "\tShaderGraphPrimitive2DVertexResult graph = EvaluatePrimitive2DShaderGraphVertex(vertex, instance);\n"
				  "\tVSOutput output = BuildPrimitive2DVertexOutput(graph.position, vertex.uv, instance);\n"
				  "\toutput.uvCoordinates.xy = vertex.position.xy;\n"
				  "\treturn output;\n"
				  "}\n";
		return source;
	}

} // Engine::ShaderGraphStageSource
