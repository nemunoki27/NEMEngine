#include "RenderPathGraphImporter.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/RenderPathGraph/RenderPathGraphNodeFactory.h>
#include <Engine/Editor/RenderPathGraph/RenderPathGraphTypes.h>

//============================================================================
//	RenderPathGraphImporter classMethods
//============================================================================

bool Engine::RenderPathGraphImporter::Import(
	const SceneHeader& sceneHeader, const std::string& scenePath, GraphDocument& outDocument) const {

	GraphDocument document{};
	// Sceneから取り込んだGraphであることが分かるようにmetadataへ元情報を残す
	document.graphType = RenderPathGraph::kGraphType;
	document.version = 1;
	document.metadata["sourceScenePath"] = scenePath;
	document.metadata["sceneName"] = sceneHeader.name;
	document.metadata["sceneAssetId"] = ToString(sceneHeader.guid);

	SceneHeader tempHeader = sceneHeader;
	tempHeader.passOrder.clear();

	GraphID previousFlowOut = 0;
	for (uint32_t i = 0; i < sceneHeader.passOrder.size(); ++i) {

		// 既存JSON仕様の未知項目を落としにくくするため、1 pass分のraw jsonをpropertiesへ残す
		tempHeader.passOrder.clear();
		tempHeader.passOrder.push_back(sceneHeader.passOrder[i]);
		nlohmann::json rawPass = ToJson(tempHeader)["passOrder"][0];

		const ImVec2 position{ 80.0f + static_cast<float>(i) * 300.0f, 120.0f };
		GraphNode node = RenderPathGraphNodeFactory::CreatePassNode(
			document, sceneHeader.passOrder[i], i, rawPass, position);

		// 作成したNodeからFlow Pinを取り出し、前のPassと接続する
		GraphID flowIn = 0;
		GraphID flowOut = 0;
		for (const GraphPin& pin : node.inputs) {
			if (pin.valueType == GraphValueType::Flow) {
				flowIn = pin.id;
				break;
			}
		}
		for (const GraphPin& pin : node.outputs) {
			if (pin.valueType == GraphValueType::Flow) {
				flowOut = pin.id;
				break;
			}
		}

		document.nodes.emplace_back(std::move(node));
		if (previousFlowOut != 0 && flowIn != 0) {
			// SceneHeader.passOrderの並び順をFlow Linkとして表現する
			GraphLink link{};
			link.id = document.GenerateID();
			link.fromPinID = previousFlowOut;
			link.toPinID = flowIn;
			document.links.emplace_back(link);
		}
		if (flowOut != 0) {
			previousFlowOut = flowOut;
		}
	}

	outDocument = std::move(document);
	return true;
}
