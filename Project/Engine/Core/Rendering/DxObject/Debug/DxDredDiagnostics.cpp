#include "DxDredDiagnostics.h"

// engine
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// c++
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace Engine::DxDredDiagnostics {

	namespace {

		std::atomic_bool g_dumped{ false };
		std::recursive_mutex g_dumpMutex;

		const char* SafeName(const char* name) {
			return name ? name : "<名前なし>";
		}

		const char* ToBreadcrumbOpName(D3D12_AUTO_BREADCRUMB_OP op) {
			switch (op) {
			case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SETMARKER";
			case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BEGINEVENT";
			case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "ENDEVENT";
			case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DRAWINSTANCED";
			case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DRAWINDEXEDINSTANCED";
			case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "DISPATCH";
			case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return "COPYBUFFERREGION";
			case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "COPYTEXTUREREGION";
			case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER: return "RESOURCEBARRIER";
			case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: return "EXECUTEINDIRECT";
			case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW: return "CLEARRENDERTARGETVIEW";
			case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW: return "CLEARUNORDEREDACCESSVIEW";
			case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA: return "RESOLVEQUERYDATA";
#if defined(D3D12_AUTO_BREADCRUMB_OP_PRESENT)
			case D3D12_AUTO_BREADCRUMB_OP_PRESENT: return "PRESENT";
#endif
			default: return "その他";
			}
		}

		std::string ConvertWideToMultiByte(const wchar_t* wstr) {
			if (!wstr) return "";
			int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 0) return "";
			std::string result(size - 1, 0);
			WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], size, nullptr, nullptr);
			return result;
		}

		void DumpBreadcrumbNode(const D3D12_AUTO_BREADCRUMB_NODE1* node) {
			if (!node) return;

			const uint32_t count = node->BreadcrumbCount;
			uint32_t completed = 0u;
			if (node->pLastBreadcrumbValue) {
				completed = *node->pLastBreadcrumbValue;
			}
			completed = (std::min)(completed, count);

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[DRED] キュー='{}' コマンドリスト='{}' パンくず数={} 完了数={}",
				SafeName(node->pCommandQueueDebugNameA),
				SafeName(node->pCommandListDebugNameA),
				count, completed);

			if (node->pBreadcrumbContexts && node->BreadcrumbContextsCount > 0u) {
				for (uint32_t i = 0; i < node->BreadcrumbContextsCount; ++i) {
					const auto& context = node->pBreadcrumbContexts[i];
					Logger::Output(LogType::Engine, spdlog::level::err,
						"[DRED]   コンテキスト パンくず番号={} 内容='{}'",
						context.BreadcrumbIndex,
						ConvertWideToMultiByte(context.pContextString));
				}
			}

			if (!node->pCommandHistory || count == 0u) return;

			constexpr uint32_t kDumpRadius = 12u;
			const uint32_t begin = completed > kDumpRadius ? completed - kDumpRadius : 0u;
			const uint32_t end = (std::min)(count, completed + kDumpRadius + 1u);

			for (uint32_t i = begin; i < end; ++i) {
				Logger::Output(LogType::Engine, spdlog::level::err,
					"[DRED]   {} [{}] {} ({})",
					i == completed ? ">>>" : "   ",
					i,
					ToBreadcrumbOpName(node->pCommandHistory[i]),
					static_cast<uint32_t>(node->pCommandHistory[i]));
			}
		}

		void DumpBreadcrumbs(ID3D12Device* device) {
			ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
			if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) return;

			D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 output{};
			if (FAILED(dred->GetAutoBreadcrumbsOutput1(&output))) return;

			constexpr uint32_t kMaxBreadcrumbNodes = 256u;
			uint32_t nodeCount = 0;
			for (const auto* node = output.pHeadAutoBreadcrumbNode;
				node != nullptr && nodeCount < kMaxBreadcrumbNodes;
				node = node->pNext) {
				DumpBreadcrumbNode(node);
				++nodeCount;
			}
		}

		void DumpAllocationList(std::string_view label, const D3D12_DRED_ALLOCATION_NODE1* head) {
			Logger::Output(LogType::Engine, spdlog::level::err, "[DRED] {}", label);

			constexpr uint32_t kMaxAllocationNodes = 256u;
			uint32_t count = 0u;
			for (const auto* node = head; node != nullptr && count < kMaxAllocationNodes; node = node->pNext) {
				Logger::Output(LogType::Engine, spdlog::level::err,
					"[DRED]   [{}] 名前='{}' 種別={} オブジェクト={}",
					count,
					SafeName(node->ObjectNameA),
					static_cast<uint32_t>(node->AllocationType),
					static_cast<const void*>(node->pObject));
				++count;
			}

			if (count == 0u) {
				Logger::Output(LogType::Engine, spdlog::level::err, "[DRED]   <なし>");
			} else if (count >= kMaxAllocationNodes) {
				Logger::Output(LogType::Engine, spdlog::level::warn,
					"[DRED] 割り当て一覧を上限件数で打ち切りました");
			}
		}

		void DumpPageFault(ID3D12Device* device) {
			ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
			if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) return;

			D3D12_DRED_PAGE_FAULT_OUTPUT1 output{};
			if (FAILED(dred->GetPageFaultAllocationOutput1(&output))) return;

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[DRED] ページフォールト仮想アドレス=0x{:016X}",
				static_cast<uint64_t>(output.PageFaultVA));

			DumpAllocationList("既存の割り当て:", output.pHeadExistingAllocationNode);
			DumpAllocationList("直近で解放した割り当て:", output.pHeadRecentFreedAllocationNode);
		}

		bool IsDeviceRemovedHRESULT(HRESULT result) {
			return result == DXGI_ERROR_DEVICE_REMOVED ||
				result == DXGI_ERROR_DEVICE_HUNG ||
				result == DXGI_ERROR_DEVICE_RESET ||
				result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
		}
	}

	void EnableBeforeDeviceCreation() {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> settings1;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&settings1))) && settings1) {
			settings1->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			settings1->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			settings1->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			Logger::Output(LogType::Engine, "[DRED] Settings1を有効にしました");
			return;
		}

		ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
		if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&settings))) && settings) {
			settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			Logger::Output(LogType::Engine, "[DRED] 旧設定インターフェースを有効にしました");
			return;
		}

		Logger::Output(LogType::Engine, spdlog::level::warn,
			"[DRED] 設定インターフェースを使用できません");
#endif
	}

	void DumpDeviceRemovedData(ID3D12Device* device, std::string_view operation) {
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		if (!device) return;

		std::lock_guard<std::recursive_mutex> lock(g_dumpMutex);
		if (g_dumped.load()) return;

		Logger::Output(LogType::Engine, spdlog::level::err, "[DRED] ===== 診断開始 =====");
		Logger::Output(LogType::Engine, spdlog::level::err, "[DRED] 操作='{}'", operation);

		const HRESULT reason = device->GetDeviceRemovedReason();
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[DRED] デバイス削除理由=0x{:08X}", static_cast<uint32_t>(reason));

		DumpBreadcrumbs(device);
		DumpPageFault(device);

		Logger::Output(LogType::Engine, spdlog::level::err, "[DRED] ===== 診断終了 =====");
		Logger::FlushAll();

		g_dumped.store(true);
#endif
	}

	bool CheckHRESULT(ID3D12Device* device, HRESULT result, std::string_view operation) {
		if (SUCCEEDED(result)) return true;

		if (IsDeviceRemovedHRESULT(result)) {
			DumpDeviceRemovedData(device, operation);
		} else if (device && FAILED(device->GetDeviceRemovedReason())) {
			DumpDeviceRemovedData(device, operation);
		}

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[D3D12] HRESULT失敗 操作='{}' HRESULT=0x{:08X}",
			operation, static_cast<uint32_t>(result));
		Logger::FlushAll();

		return false;
	}

	bool CheckDeviceState(ID3D12Device* device, std::string_view operation) {
		if (!device) return false;

		const HRESULT reason = device->GetDeviceRemovedReason();
		if (SUCCEEDED(reason)) return true;

		DumpDeviceRemovedData(device, operation);
		return false;
	}

	bool WaitForFence(ID3D12Device* device, ID3D12Fence* fence, UINT64 expectedValue,
		HANDLE completionEvent, std::string_view operation) {

		if (!device || !fence || expectedValue == UINT64_MAX) {
			return false;
		}
		bool eventRegistered = false;
		bool useEvent = completionEvent != nullptr;
		for (;;) {
			const UINT64 completed = fence->GetCompletedValue();
			// 消失時の最大値を通常の完了値と比較しない
			if (completed == UINT64_MAX || !CheckDeviceState(device, operation)) {
				DumpDeviceRemovedData(device, operation);
				return false;
			}
			if (completed >= expectedValue) {
				return true;
			}
			if (useEvent && !eventRegistered) {
				useEvent = CheckHRESULT(device, fence->SetEventOnCompletion(expectedValue, completionEvent), operation);
				eventRegistered = useEvent;
			}
			constexpr DWORD kWaitSliceMilliseconds = 250;
			if (useEvent) {
				const DWORD result = WaitForSingleObject(completionEvent, kWaitSliceMilliseconds);
				if (result != WAIT_OBJECT_0 && result != WAIT_TIMEOUT) {
					CheckHRESULT(device, HRESULT_FROM_WIN32(GetLastError()), operation);
					useEvent = false;
				}
			} else {
				// イベントを使えなくても完了前に資源を解放しない
				Sleep(kWaitSliceMilliseconds);
			}
		}
	}

	bool WaitForEvent(ID3D12Device* device, HANDLE event, std::string_view operation) {

		if (!event) return false;
		while (CheckDeviceState(device, operation)) {
			const DWORD result = WaitForSingleObject(event, 250);
			if (result == WAIT_OBJECT_0) return CheckDeviceState(device, operation);
			if (result != WAIT_TIMEOUT) {
				CheckHRESULT(device, HRESULT_FROM_WIN32(GetLastError()), operation);
				return false;
			}
		}
		return false;
	}

	void ResetForNewDevice() {
		g_dumped.store(false);
	}
}
