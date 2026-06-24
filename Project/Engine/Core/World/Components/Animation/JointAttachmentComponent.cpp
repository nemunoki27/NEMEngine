#include "JointAttachmentComponent.h"

//============================================================================
//	JointAttachmentComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, JointAttachmentComponent& component) {

	const std::string skinnedLocalFileID = in.value("skinnedEntityLocalFileID", "");
	component.skinnedEntityLocalFileID = skinnedLocalFileID.empty() ? UUID{} : FromString16Hex(skinnedLocalFileID);
	component.jointName = in.value("jointName", "");
}

void Engine::to_json(nlohmann::json& out, const JointAttachmentComponent& component) {

	out["skinnedEntityLocalFileID"] =
		component.skinnedEntityLocalFileID ? ToString(component.skinnedEntityLocalFileID) : "";
	out["jointName"] = component.jointName;
}
