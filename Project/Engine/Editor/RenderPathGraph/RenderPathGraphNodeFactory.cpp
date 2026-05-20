#include "RenderPathGraphNodeFactory.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/RenderPathGraph/RenderPathGraphTypes.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

//============================================================================
//	RenderPathGraphNodeFactory classMethods
//============================================================================

namespace {

	Engine::GraphPinDefinition FlowIn(bool required = false) {
		// Passの実行順入力Pin
		return Engine::GraphPinDefinition{
			.kind = Engine::GraphPinKind::Input,
			.valueType = Engine::GraphValueType::Flow,
			.name = "In",
			.required = required,
		};
	}

	Engine::GraphPinDefinition FlowOut() {
		// Passの実行順出力Pin
		return Engine::GraphPinDefinition{
			.kind = Engine::GraphPinKind::Output,
			.valueType = Engine::GraphValueType::Flow,
			.name = "Out",
		};
	}

	nlohmann::json MakeDefaultProperties() {

		// 全Pass Nodeで共通する既定値
		nlohmann::json data = nlohmann::json::object();
		data["enabled"] = true;
		data["raw"] = nlohmann::json::object();
		return data;
	}

	void RegisterPassDefinition(Engine::GraphNodeRegistry& registry,
		const char* type, const char* name, const ImVec4& color, std::initializer_list<Engine::GraphPinDefinition> pins) {

		// ScenePassに対応するNode定義をまとめて登録する
		Engine::GraphNodeDefinition definition{};
		definition.type = type;
		definition.displayName = name;
		definition.category = "RenderPath";
		definition.accentColor = color;
		definition.defaultProperties = MakeDefaultProperties();
		definition.pins = pins;
		registry.Register(definition);
	}

	std::string FirstColorName(const Engine::RenderTargetSetReference& reference) {

		// 現状UIには先頭Colorだけ簡易表示する
		return reference.colors.empty() ? "" : reference.colors.front();
	}
}

void Engine::RenderPathGraphNodeFactory::RegisterDefinitions(GraphNodeRegistry& registry) {

	// SceneHeader.passOrderで使うPass Node
	RegisterPassDefinition(registry, RenderPathGraph::kClear, "Clear", ImVec4(0.35f, 0.50f, 0.72f, 1.0f),
		{ FlowIn(), FlowOut() });
	RegisterPassDefinition(registry, RenderPathGraph::kDepthPrepass, "DepthPrepass", ImVec4(0.58f, 0.42f, 0.85f, 1.0f),
		{ FlowIn(true), FlowOut(),
			{ GraphPinKind::Output, GraphValueType::DepthTexture, "Depth", false, false } });
	RegisterPassDefinition(registry, RenderPathGraph::kDraw, "Draw", ImVec4(0.34f, 0.72f, 0.42f, 1.0f),
		{ FlowIn(true), FlowOut() });
	RegisterPassDefinition(registry, RenderPathGraph::kCompute, "Compute", ImVec4(0.25f, 0.75f, 0.80f, 1.0f),
		{ FlowIn(true),
			{ GraphPinKind::Input, GraphValueType::Texture2D, "SourceColor", false, false },
			{ GraphPinKind::Input, GraphValueType::DepthTexture, "SourceDepth", false, false },
			FlowOut(),
			{ GraphPinKind::Output, GraphValueType::Texture2DUAV, "DestColor", false, false } });
	RegisterPassDefinition(registry, RenderPathGraph::kPostProcess, "PostProcess", ImVec4(0.78f, 0.34f, 0.76f, 1.0f),
		{ FlowIn(true),
			{ GraphPinKind::Input, GraphValueType::Texture2D, "SourceColor", true, false },
			{ GraphPinKind::Input, GraphValueType::DepthTexture, "SourceDepth", false, false },
			FlowOut(),
			{ GraphPinKind::Output, GraphValueType::Texture2DUAV, "DestColor", false, false } });
	RegisterPassDefinition(registry, RenderPathGraph::kBlit, "Blit", ImVec4(0.85f, 0.55f, 0.25f, 1.0f),
		{ FlowIn(true),
			{ GraphPinKind::Input, GraphValueType::Texture2D, "SourceColor", false, false },
			FlowOut(),
			{ GraphPinKind::Output, GraphValueType::View, "View", false, false } });
	RegisterPassDefinition(registry, RenderPathGraph::kRaytracing, "Raytracing", ImVec4(0.78f, 0.25f, 0.28f, 1.0f),
		{ FlowIn(true), FlowOut() });
	RegisterPassDefinition(registry, RenderPathGraph::kRenderScene, "RenderScene", ImVec4(0.34f, 0.72f, 0.42f, 1.0f),
		{ FlowIn(true), FlowOut() });
	RegisterPassDefinition(registry, RenderPathGraph::kFullscreenCopy, "FullscreenCopy", ImVec4(0.85f, 0.55f, 0.25f, 1.0f),
		{ FlowIn(true), FlowOut() });

	// 一時RenderTargetはPassではなくResource Nodeとして扱う
	GraphNodeDefinition temporary{};
	temporary.type = RenderPathGraph::kTemporaryTarget;
	temporary.displayName = "TemporaryTarget";
	temporary.category = "RenderPath";
	temporary.defaultProperties = {
		{ "name", "PostProcessDebugTemp" },
		{ "format", "RGBA32_FLOAT" },
		{ "widthScale", 1.0f },
		{ "heightScale", 1.0f },
		{ "createUAV", true },
		{ "persistent", false },
	};
	temporary.pins = {
		{ GraphPinKind::Output, GraphValueType::Texture2DUAV, "Texture", false, true },
	};
	registry.Register(temporary);

	// 最終表示先を表すView Node
	GraphNodeDefinition view{};
	view.type = RenderPathGraph::kView;
	view.displayName = "View";
	view.category = "RenderPath";
	view.defaultProperties = { { "name", "View" } };
	view.pins = {
		{ GraphPinKind::Input, GraphValueType::Texture2D, "Color", false, false },
	};
	registry.Register(view);
}

const char* Engine::RenderPathGraphNodeFactory::ToNodeType(ScenePassType type) {

	// ScenePassTypeをGraph保存用のNode Type文字列に変換する
	switch (type) {
	case ScenePassType::Clear:
		return RenderPathGraph::kClear;
	case ScenePassType::DepthPrepass:
		return RenderPathGraph::kDepthPrepass;
	case ScenePassType::Draw:
		return RenderPathGraph::kDraw;
	case ScenePassType::PostProcess:
		return RenderPathGraph::kPostProcess;
	case ScenePassType::Compute:
		return RenderPathGraph::kCompute;
	case ScenePassType::RenderScene:
		return RenderPathGraph::kRenderScene;
	case ScenePassType::Blit:
		return RenderPathGraph::kBlit;
	case ScenePassType::Raytracing:
		return RenderPathGraph::kRaytracing;
	default:
		return RenderPathGraph::kUnknown;
	}
}

Engine::ScenePassType Engine::RenderPathGraphNodeFactory::ToPassType(const std::string& nodeType) {

	// Unknown NodeはCompile対象外だが、保険としてDrawへ落とす
	if (nodeType == RenderPathGraph::kClear) {
		return ScenePassType::Clear;
	}
	if (nodeType == RenderPathGraph::kDepthPrepass) {
		return ScenePassType::DepthPrepass;
	}
	if (nodeType == RenderPathGraph::kDraw) {
		return ScenePassType::Draw;
	}
	if (nodeType == RenderPathGraph::kPostProcess) {
		return ScenePassType::PostProcess;
	}
	if (nodeType == RenderPathGraph::kCompute) {
		return ScenePassType::Compute;
	}
	if (nodeType == RenderPathGraph::kRenderScene) {
		return ScenePassType::RenderScene;
	}
	if (nodeType == RenderPathGraph::kBlit || nodeType == RenderPathGraph::kFullscreenCopy) {
		return ScenePassType::Blit;
	}
	if (nodeType == RenderPathGraph::kRaytracing) {
		return ScenePassType::Raytracing;
	}
	return ScenePassType::Draw;
}

bool Engine::RenderPathGraphNodeFactory::IsScenePassNode(const std::string& nodeType) {

	// Resource NodeやComment NodeはScenePassDescを生成しない
	return nodeType == RenderPathGraph::kClear ||
		nodeType == RenderPathGraph::kDepthPrepass ||
		nodeType == RenderPathGraph::kDraw ||
		nodeType == RenderPathGraph::kPostProcess ||
		nodeType == RenderPathGraph::kCompute ||
		nodeType == RenderPathGraph::kRenderScene ||
		nodeType == RenderPathGraph::kBlit ||
		nodeType == RenderPathGraph::kFullscreenCopy ||
		nodeType == RenderPathGraph::kRaytracing;
}

Engine::GraphNode Engine::RenderPathGraphNodeFactory::CreatePassNode(
	GraphDocument& document, const ScenePassDesc& pass, uint32_t passIndex,
	const nlohmann::json& rawPass, const ImVec2& position) {

	GraphNodeRegistry registry{};
	RegisterDefinitions(registry);

	// Pass種別に対応したNode定義から土台を作る
	GraphNode node = registry.CreateNode(document, ToNodeType(pass.type), position);
	node.enabled = pass.enabled;
	node.properties["enabled"] = pass.enabled;
	node.properties["passIndex"] = passIndex;
	node.properties["raw"] = rawPass;

	switch (pass.type) {
	case ScenePassType::Clear:
		// Clearは対象とClear値を表示用Propertiesへ抜き出す
		node.displayName = "Clear";
		node.properties["target"] = FirstColorName(pass.clear.dest);
		node.properties["clearColor"] = pass.clear.clearColor;
		node.properties["clearDepth"] = pass.clear.clearDepth;
		break;
	case ScenePassType::DepthPrepass:
		// DepthPrepassはQueue / PassNameを編集対象にする
		node.displayName = "DepthPrepass : " + pass.depthPrepass.queue;
		node.properties["queue"] = pass.depthPrepass.queue;
		node.properties["passName"] = pass.depthPrepass.passName;
		node.properties["dest"] = rawPass.value("dest", nlohmann::json::object());
		break;
	case ScenePassType::Draw:
		// DrawはQueue名がNode名で分かるようにする
		node.displayName = pass.draw.queue.empty() ? "Draw" : "Draw : " + pass.draw.queue;
		node.properties["queue"] = pass.draw.queue;
		node.properties["passName"] = pass.draw.passName;
		node.properties["dest"] = rawPass.value("dest", nlohmann::json::object());
		break;
	case ScenePassType::PostProcess:
		// PostProcessはMaterialと入出力Targetを保持する
		node.displayName = "PostProcess";
		node.properties["material"] = ToString(pass.postProcess.material);
		node.properties["source"] = rawPass.value("source", nlohmann::json::object());
		node.properties["dest"] = rawPass.value("dest", nlohmann::json::object());
		node.properties["extraSources"] = rawPass.value("extraSources", nlohmann::json::object());
		break;
	case ScenePassType::Compute:
		// ComputeはMaterial / Dispatch設定 / 入出力Targetを保持する
		node.displayName = "Compute : " + pass.compute.passName;
		node.properties["material"] = ToString(pass.compute.material);
		node.properties["passName"] = pass.compute.passName;
		node.properties["dispatchMode"] = EnumAdapter<ComputeDispatchMode>::ToString(pass.compute.dispatchMode);
		node.properties["source"] = rawPass.value("source", nlohmann::json::object());
		node.properties["dest"] = rawPass.value("dest", nlohmann::json::object());
		break;
	case ScenePassType::RenderScene:
		// RenderSceneはSubScene Slotを編集対象にする
		node.displayName = "RenderScene : " + pass.renderScene.subSceneSlot;
		node.properties["subSceneSlot"] = pass.renderScene.subSceneSlot;
		node.properties["dest"] = rawPass.value("dest", nlohmann::json::object());
		break;
	case ScenePassType::Blit:
		// Blitは表示先へ渡すSource / Destを保持する
		node.displayName = "Blit";
		node.properties["material"] = ToString(pass.blit.material);
		node.properties["source"] = rawPass.value("source", nlohmann::json::object());
		node.properties["dest"] = rawPass.value("dest", nlohmann::json::object());
		break;
	case ScenePassType::Raytracing:
		// RaytracingはMaterialとPassNameを保持する
		node.displayName = "Raytracing : " + pass.raytracing.passName;
		node.properties["material"] = ToString(pass.raytracing.material);
		node.properties["passName"] = pass.raytracing.passName;
		node.properties["source"] = rawPass.value("source", nlohmann::json::object());
		node.properties["dest"] = rawPass.value("dest", nlohmann::json::object());
		break;
	default:
		break;
	}
	return node;
}
