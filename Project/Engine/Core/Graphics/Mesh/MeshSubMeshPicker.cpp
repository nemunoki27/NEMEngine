#include "MeshSubMeshPicker.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/GraphicsPlatform.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>

//============================================================================
//	MeshSubMeshPicker classMethods
//============================================================================

void Engine::MeshSubMeshPicker::Init(GraphicsCore& graphicsCore) {

	// すでに初期化されているなら何もしない
	if (initialized_) {
		return;
	}

	auto& platform = graphicsCore.GetDXObject();

	// ピック用のコンピュートシェーダーのパイプラインを生成
	ComputePipelineDesc desc{};
	desc.compute.file = "Builtin/Editor/PickMeshInstance.CS.hlsl";
	desc.compute.entry = "main";
	desc.compute.profile = "cs_6_6";
	pipeline_.CreateCompute(platform.GetDevice(), platform.GetDxShaderCompiler(), desc);

	// バッファの生成
	pickingBuffer_.CreateBuffer(platform.GetDevice());
	outputBuffer_.CreateUAVBuffer(platform.GetDevice(), 1);
	readbackBuffer_.CreateBuffer(platform.GetDevice());

	// 出力バッファは初期状態ではCOMMON
	outputState_ = D3D12_RESOURCE_STATE_COMMON;
	pendingReadback_ = false;
	pendingRecords_.clear();
	initialized_ = true;
}

void Engine::MeshSubMeshPicker::Finalize() {

	pendingRecords_.clear();
	outputState_ = D3D12_RESOURCE_STATE_COMMON;
	initialized_ = false;
	pendingReadback_ = false;
}

void Engine::MeshSubMeshPicker::ConsumePendingResult(ECSWorld* world, EditorState& editorState) {

	// ピック結果のreadbackが完了していないなら何もしない
	if (!pendingReadback_) {
		return;
	}
	pendingReadback_ = false;

	if (!world) {
		pendingRecords_.clear();
		return;
	}

	// ピック結果を取得
	const PickResult result = readbackBuffer_.GetReadbackData();

	// ヒットなし
	if (result.instanceID == kInvalidPickInstanceID ||
		result.instanceID >= pendingRecords_.size()) {
		pendingRecords_.clear();
		return;
	}

	// ピック結果に対応するサブメッシュ情報を取得
	const auto& record = pendingRecords_[result.instanceID];
	pendingRecords_.clear();
	if (!world->IsAlive(record.entity)) {
		return;
	}

	// エディタの選択状態を更新
	editorState.SelectFromScenePick(record.entity, record.subMeshIndex, record.subMeshStableID);
}

void Engine::MeshSubMeshPicker::ExecutePick(GraphicsCore& graphicsCore, const ResolvedRenderView& view,
	const Vector2& inputPixel, std::span<const MeshSubMeshPickRecord> pickRecords, ID3D12Resource* tlasResource) {

	// 無効な状態のときは何もしない
	if (!initialized_ || !tlasResource) {
		return;
	}

	// カメラ情報を取得
	const ResolvedCameraView* camera = view.FindCamera(RenderCameraDomain::Perspective);
	if (!camera) {
		return;
	}

	// メッシュが1つも無いなら空選択扱い
	if (pickRecords.empty()) {
		pendingRecords_.clear();
		pendingReadback_ = false;
		return;
	}

	// ピック用のデータを構築してGPUへ転送
	PickingData data{};
	data.inputPixelX = static_cast<uint32_t>(std::clamp(inputPixel.x, 0.0f, static_cast<float>(view.width - 1)));
	data.inputPixelY = static_cast<uint32_t>(std::clamp(inputPixel.y, 0.0f, static_cast<float>(view.height - 1)));
	data.textureWidth = view.width;
	data.textureHeight = view.height;
	data.inverseViewProjection = Matrix4x4::Inverse(camera->matrices.viewProjectionMatrix);
	data.cameraWorldPos = camera->cameraPos;
	data.rayMax = camera->farClip;
	pickingBuffer_.TransferData(data);

	// ピック対象のサブメッシュ情報を保持
	pendingRecords_.assign(pickRecords.begin(), pickRecords.end());

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	// 出力バッファをUAV状態に遷移
	if (outputState_ != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {

		dxCommand->TransitionBarriers({ outputBuffer_.GetResource() },
			outputState_, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		outputState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	}

	// パイプラインを設定
	commandList->SetComputeRootSignature(pipeline_.GetRootSignature());
	commandList->SetPipelineState(pipeline_.GetComputePipeline());

	// ルート引数をバインド
	ComputeRootBinder binder(pipeline_);
	const std::array<ComputeBindItem, 3> bindItems = { {
		ComputeBindItem{
			.type = ComputeBindValueType::AccelStruct,
			.gpuAddress = tlasResource->GetGPUVirtualAddress(),
			.bindPoint = 0,
			.space = 0,
		},
		ComputeBindItem{
			.type = ComputeBindValueType::UAV,
			.gpuAddress = outputBuffer_.GetResource()->GetGPUVirtualAddress(),
			.bindPoint = 0,
			.space = 0,
		},
		ComputeBindItem{
			.type = ComputeBindValueType::CBV,
			.gpuAddress = pickingBuffer_.GetResource()->GetGPUVirtualAddress(),
			.bindPoint = 0,
			.space = 0,
		},
	} };
	binder.Bind(commandList, bindItems);

	// ピック処理実行
	commandList->Dispatch(1, 1, 1);

	// UAVの書き込み順序保証のためバリアを発行
	dxCommand->UAVBarrier(outputBuffer_.GetResource());

	// CPUに書き戻す
	dxCommand->TransitionBarriers({ outputBuffer_.GetResource() },
		outputState_, D3D12_RESOURCE_STATE_COPY_SOURCE);
	outputState_ = D3D12_RESOURCE_STATE_COPY_SOURCE;
	// 出力バッファの内容をリードバック用バッファへコピー
	commandList->CopyResource(readbackBuffer_.GetResource(), outputBuffer_.GetResource());

	pendingReadback_ = true;
}