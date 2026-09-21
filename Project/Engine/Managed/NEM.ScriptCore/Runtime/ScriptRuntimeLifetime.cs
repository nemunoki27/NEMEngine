using System.Threading;

namespace NEMEngine;

//============================================================================
//	ScriptRuntimeLifetime
//	現在ロード中の GameScripts assembly の寿命を管理する。
//	reload（unload）前に、古い assembly を参照し続ける task / event / timer / IDisposable を
//	停止・解放させ、collectible ALC が確実に回収されるようにするためのサービス。
//============================================================================
// Coroutine / Timer など unload 前に停止が必要なものはここへ Register する。
// 本サービス自体は ScriptCore（default ALC）側にあり、登録物は EndAssemblyLifetime で必ず手放すため、
// 古い GameScripts assembly を永続保持しない。すべての public API は thread-safe。
public static class ScriptRuntimeLifetime {

	// 内部状態の保護
	private static readonly object gate = new();
	// 現在の assembly 寿命に紐づくキャンセルトークン源
	private static CancellationTokenSource lifetime = new();
	// unload 前に dispose する登録物
	private static readonly List<IDisposable> disposables = new();
	// unload 前に実行する購読解除アクション
	private static readonly List<Action> unsubscribes = new();

	// reload 時に cancel されるトークン。長時間 task / timer はこれを監視して停止する
	public static CancellationToken ReloadToken {
		get { lock (gate) { return lifetime.Token; } }
	}

	//========================================================================
	//	internal Methods（ホスト側 load/unload から呼ぶ）
	//========================================================================

	// assembly load 成功時に新しい寿命を開始する
	internal static void BeginAssemblyLifetime() {

		lock (gate) {

			// 直前の寿命が cancel 済みなら作り直す
			if (lifetime.IsCancellationRequested) {

				lifetime.Dispose();
				lifetime = new CancellationTokenSource();
			}
			disposables.Clear();
			unsubscribes.Clear();
		}
	}

	// unload 前に呼ぶ。token を cancel し、登録済みの解除/破棄をすべて実行する
	internal static void EndAssemblyLifetime() {

		CancellationTokenSource toCancel;
		IDisposable[] pendingDisposables;
		Action[] pendingUnsubscribes;
		lock (gate) {

			toCancel = lifetime;
			pendingDisposables = disposables.ToArray();
			pendingUnsubscribes = unsubscribes.ToArray();
			// 参照を手放す（古い assembly のオブジェクトを保持し続けないため）
			disposables.Clear();
			unsubscribes.Clear();
		}

		// まず token を cancel（coroutine/timer 等が停止できるようにする）
		try {
			toCancel.Cancel();
		}
		catch (Exception ex) {
			NativeAPI.WriteLog(2, $"[ScriptRuntimeLifetime] cancel failed\n{ex}");
		}

		// 購読解除 → dispose。例外が出ても残りの cleanup を継続する
		foreach (Action unsubscribe in pendingUnsubscribes) {
			try {
				unsubscribe();
			}
			catch (Exception ex) {
				NativeAPI.WriteLog(2, $"[ScriptRuntimeLifetime] unsubscribe failed\n{ex}");
			}
		}
		foreach (IDisposable disposable in pendingDisposables) {
			try {
				disposable.Dispose();
			}
			catch (Exception ex) {
				NativeAPI.WriteLog(2, $"[ScriptRuntimeLifetime] dispose failed\n{ex}");
			}
		}

		// gameplay service の static event / pending 状態を解放し、古い GameScripts assembly の delegate を手放す。
		// （これらの static は ScriptCore 側にあり、登録された delegate が GameScripts 型を参照するため、unload 前に必ずクリアする）
		try {
			SceneManager.ResetForReload();
			Application.ResetForReload();
			InputActions.ResetForReload();
			Timers.ResetForReload();
			Coroutines.ResetForReload();
			EventBus.ResetForReload();
			EventOwnerTracker.ResetForReload();
			EventDispatch.ResetForReload();
		}
		catch (Exception ex) {
			NativeAPI.WriteLog(2, $"[ScriptRuntimeLifetime] gameplay reset failed\n{ex}");
		}
	}

	//========================================================================
	//	public Methods（ユーザースクリプトから利用）
	//========================================================================

	// reload(unload) 前に Dispose される IDisposable を登録する。
	// 戻り値を Dispose すると登録解除できる（多重 Dispose も安全）。
	public static IDisposable Register(IDisposable disposable) {

		ArgumentNullException.ThrowIfNull(disposable);
		lock (gate) {
			disposables.Add(disposable);
		}
		return new Registration(() => {
			lock (gate) {
				disposables.Remove(disposable);
			}
		});
	}

	// reload(unload) 前に実行する購読解除アクションを登録する（static event の解除など）。
	public static IDisposable Register(Action unsubscribe) {

		ArgumentNullException.ThrowIfNull(unsubscribe);
		lock (gate) {
			unsubscribes.Add(unsubscribe);
		}
		return new Registration(() => {
			lock (gate) {
				unsubscribes.Remove(unsubscribe);
			}
		});
	}

	// 登録解除用ハンドル。多重 Dispose を安全に扱う
	private sealed class Registration : IDisposable {

		private Action? remove;

		public Registration(Action remove) {
			this.remove = remove;
		}

		public void Dispose() {
			Action? action = Interlocked.Exchange(ref remove, null);
			action?.Invoke();
		}
	}
}
