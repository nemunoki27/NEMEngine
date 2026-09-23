#include "PipelineState.h"

//============================================================================
//	include
//============================================================================
#include "PipelineShaderLoader.h"
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/Pipelines/ShaderSourcePathResolver.h>
#include <Engine/Core/Rendering/Shaders/ShaderCook.h>

// c++
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unordered_map>

using namespace Engine;
using namespace Engine::PipelineShaderLoader;

//============================================================================
//	PipelineState classMethods
//============================================================================

uint64_t Engine::PipelineState::NextUniqueID() {

	// 生成のたびに増える、0は未設定を表すため1から始める
	static std::atomic<uint64_t> counter{ 0 };
	return ++counter;
}

void Engine::PipelineState::RetireGPUObjects(GraphicsResourceRetirement& retirement) const {

	retirement.Retire(rootSignature_);
	for (const auto& pipeline : graphicsPipelines_) {
		retirement.Retire(pipeline);
	}
	retirement.Retire(computePipeline_);
}

const RootBindingLocation* PipelineState::FindBinding(
	ShaderBindingKind kind, UINT bindPoint, UINT space) const {

	auto found = registerBindingTable_.find(BindingRegisterKey{ kind, bindPoint, space });
	if (found == registerBindingTable_.end()) {
		return nullptr;
	}
	return &bindings_[found->second];
}

const RootBindingLocation* Engine::PipelineState::FindBindingByName(
	const std::string_view& name, ShaderBindingKind kind) const {

	const auto& table = nameBindingTables_[static_cast<size_t>(kind)];
	auto found = table.find(name);
	if (found == table.end()) {
		return nullptr;
	}
	return &bindings_[found->second];
}

ID3D12PipelineState* Engine::PipelineState::GetGraphicsPipeline(BlendMode blendMode) const {

	return graphicsPipelines_[static_cast<uint32_t>(blendMode)].Get();
}

void Engine::PipelineState::RebuildBindingLookupTables() {

	registerBindingTable_.clear();
	for (auto& table : nameBindingTables_) {
		table.clear();
	}
	registerBindingTable_.reserve(bindings_.size());
	for (auto& table : nameBindingTables_) {

		table.reserve(bindings_.size());
	}
	for (size_t i = 0; i < bindings_.size(); ++i) {

		const auto& bind = bindings_[i];
		// register/space/kindから引くためのテーブルと、名前から引くためのテーブルの両方に登録する
		registerBindingTable_[BindingRegisterKey{ bind.kind, bind.bindPoint, bind.space }] = i;
		nameBindingTables_[static_cast<size_t>(bind.kind)][bind.name] = i;
	}
}

//============================================================================
//	PipelineState classMethods
//============================================================================

namespace Engine {

	bool PipelineState::BindingRegisterKey::operator==(const BindingRegisterKey& rhs) const noexcept {

		return kind == rhs.kind && bindPoint == rhs.bindPoint && space == rhs.space;
	}

	size_t PipelineState::BindingRegisterKeyHash::operator()(const BindingRegisterKey& key) const noexcept {

		size_t h1 = std::hash<uint32_t>{}(static_cast<uint32_t>(key.kind));
		size_t h2 = std::hash<UINT>{}(key.bindPoint);
		size_t h3 = std::hash<UINT>{}(key.space);
		size_t result = h1;
		result ^= h2 + 0x9e3779b9 + (result << 6) + (result >> 2);
		result ^= h3 + 0x9e3779b9 + (result << 6) + (result >> 2);
		return result;
	}
}

Engine::PipelineState::~PipelineState() {

	if (retirement_) {
		RetireGPUObjects(*retirement_);
	}
}

void Engine::PipelineState::SetRetirementQueue(GraphicsResourceRetirement& retirement) {

	Assert::Call(!retirement_ || retirement_ == &retirement, "使用中のPipelineの回収窓口は変更できません");
	retirement_ = &retirement;
}
