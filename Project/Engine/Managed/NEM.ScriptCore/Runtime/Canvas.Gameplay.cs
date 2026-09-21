using System;
using System.Collections.Generic;

namespace NEMEngine;

public enum CanvasInputAction {

    Up,
    Down,
    Left,
    Right,
    Submit
}

public sealed unsafe partial class Canvas {

    private const int KeyboardInputDevice = 0;
    private const int GamepadInputDevice = 1;

    private CanvasTransitionTable? transitionTable;

    // 行列形式のUI遷移テーブル
    public CanvasTransitionTable TransitionTable =>
        transitionTable ??= new CanvasTransitionTable(entity);

    // 決定後にCanvas入力がロックされているか
    public bool InputLocked =>
        NativeAPI.GetCanvasInputLocked != null &&
        NativeAPI.GetCanvasInputLocked(entity.native) != 0;

    // GameViewピクセル座標をCanvasローカル座標へ変換する
    public bool TryScreenToLocalPoint(
        Vector2 screenPosition, out Vector2 localPosition) {

        return NativeAPI.ReadCanvasScreenToLocalPoint(
            entity.native, screenPosition, out localPosition);
    }

    // 指定操作に割り当てたキーボード入力を取得する
    public IReadOnlyList<KeyCode> GetKeyboardInputs(CanvasInputAction action) {

        int[] codes = NativeAPI.CanvasGetInputBindings(
            entity.native, (int)action, KeyboardInputDevice);
        KeyCode[] bindings = new KeyCode[codes.Length];
        for (int i = 0; i < codes.Length; ++i) {
            bindings[i] = (KeyCode)codes[i];
        }
        return bindings;
    }

    // 指定操作に割り当てたゲームパッド入力を取得する
    public IReadOnlyList<GamepadButton> GetGamepadInputs(CanvasInputAction action) {

        int[] codes = NativeAPI.CanvasGetInputBindings(
            entity.native, (int)action, GamepadInputDevice);
        GamepadButton[] bindings = new GamepadButton[codes.Length];
        for (int i = 0; i < codes.Length; ++i) {
            bindings[i] = (GamepadButton)codes[i];
        }
        return bindings;
    }

    // 指定操作のキーボード入力を置き換える
    public void SetKeyboardInputs(CanvasInputAction action, params KeyCode[] keys) {

        ArgumentNullException.ThrowIfNull(keys);
        int[] bindings = new int[keys.Length];
        for (int i = 0; i < keys.Length; ++i) {
            bindings[i] = (int)keys[i];
        }
        NativeAPI.CanvasSetInputBindingsValue(
            entity.native, (int)action, KeyboardInputDevice, bindings);
    }

    // 指定操作のゲームパッド入力を置き換える
    public void SetGamepadInputs(CanvasInputAction action, params GamepadButton[] buttons) {

        ArgumentNullException.ThrowIfNull(buttons);
        int[] bindings = new int[buttons.Length];
        for (int i = 0; i < buttons.Length; ++i) {
            bindings[i] = (int)buttons[i];
        }
        NativeAPI.CanvasSetInputBindingsValue(
            entity.native, (int)action, GamepadInputDevice, bindings);
    }

    // 指定操作へキーボード入力を追加する
    public void AddKeyboardInput(CanvasInputAction action, KeyCode key) {

        List<KeyCode> bindings = new(GetKeyboardInputs(action));
        if (!bindings.Contains(key)) {
            bindings.Add(key);
            SetKeyboardInputs(action, bindings.ToArray());
        }
    }

    // 指定操作へゲームパッド入力を追加する
    public void AddGamepadInput(CanvasInputAction action, GamepadButton button) {

        List<GamepadButton> bindings = new(GetGamepadInputs(action));
        if (!bindings.Contains(button)) {
            bindings.Add(button);
            SetGamepadInputs(action, bindings.ToArray());
        }
    }

    // 指定操作からキーボード入力を削除する
    public bool RemoveKeyboardInput(CanvasInputAction action, KeyCode key) {

        List<KeyCode> bindings = new(GetKeyboardInputs(action));
        if (!bindings.Remove(key)) {
            return false;
        }
        SetKeyboardInputs(action, bindings.ToArray());
        return true;
    }

    // 指定操作からゲームパッド入力を削除する
    public bool RemoveGamepadInput(CanvasInputAction action, GamepadButton button) {

        List<GamepadButton> bindings = new(GetGamepadInputs(action));
        if (!bindings.Remove(button)) {
            return false;
        }
        SetGamepadInputs(action, bindings.ToArray());
        return true;
    }

    // 指定操作のキーボード入力をすべて削除する
    public void ClearKeyboardInputs(CanvasInputAction action) {
        SetKeyboardInputs(action, Array.Empty<KeyCode>());
    }

    // 指定操作のゲームパッド入力をすべて削除する
    public void ClearGamepadInputs(CanvasInputAction action) {
        SetGamepadInputs(action, Array.Empty<GamepadButton>());
    }
}

// Canvas遷移テーブルを行列で操作する
public sealed class CanvasTransitionTable {

    private enum Result {

        Success,
        InvalidCanvas,
        InvalidSize,
        OutOfRange,
        InvalidTarget,
        AllocationFailed
    }

    private readonly Entity canvas;

    internal CanvasTransitionTable(Entity canvas) {
        this.canvas = canvas;
    }

    public int Rows {
        get {
            GetSize(out int rows, out _);
            return rows;
        }
    }

    public int Columns {
        get {
            GetSize(out _, out int columns);
            return columns;
        }
    }

    public CanvasTransitionRow this[int row] {
        get {
            GetSize(out int rows, out _);
            if (row < 0 || rows <= row) {
                throw new ArgumentOutOfRangeException(nameof(row));
            }
            return new CanvasTransitionRow(this, row);
        }
    }

    public Entity this[int row, int column] {
        get {
            ValidateIndices(row, column);
            int result = NativeAPI.ReadCanvasNavigationCell(
                canvas.native, row, column, out Entity target);
            ThrowIfFailed(result, nameof(row), nameof(column));
            return target;
        }
        set {
            ValidateIndices(row, column);
            int result = NativeAPI.WriteCanvasNavigationCell(
                canvas.native, row, column, value);
            ThrowIfFailed(result, nameof(row), nameof(value));
        }
    }

    public void Resize(int rows, int columns) {

        if (rows <= 0) {
            throw new ArgumentOutOfRangeException(nameof(rows));
        }
        if (columns <= 0) {
            throw new ArgumentOutOfRangeException(nameof(columns));
        }
        int result = NativeAPI.ResizeCanvasNavigationTableValue(
            canvas.native, rows, columns);
        ThrowIfFailed(result, nameof(rows), nameof(columns));
    }

    private void GetSize(out int rows, out int columns) {

        int result = NativeAPI.ReadCanvasNavigationTableSize(
            canvas.native, out rows, out columns);
        ThrowIfFailed(result, nameof(canvas), nameof(canvas));
    }

    private void ValidateIndices(int row, int column) {

        GetSize(out int rows, out int columns);
        if (row < 0 || rows <= row) {
            throw new ArgumentOutOfRangeException(nameof(row));
        }
        if (column < 0 || columns <= column) {
            throw new ArgumentOutOfRangeException(nameof(column));
        }
    }

    private static void ThrowIfFailed(
        int result, string rangeParameterName, string targetParameterName) {

        switch ((Result)result) {
        case Result.Success:
            return;
        case Result.InvalidCanvas:
            throw new InvalidOperationException("Canvasが無効か、既に破棄されています");
        case Result.InvalidSize:
        case Result.OutOfRange:
            throw new ArgumentOutOfRangeException(rangeParameterName);
        case Result.InvalidTarget:
            throw new ArgumentException(
                "遷移先は同じCanvas配下のUISelectableを持つEntityにしてください",
                targetParameterName);
        case Result.AllocationFailed:
            throw new OutOfMemoryException("Canvas遷移テーブルの領域を確保できませんでした");
        default:
            throw new InvalidOperationException("Canvas遷移テーブルの操作に失敗しました");
        }
    }
}

// Canvas遷移テーブルの1行を操作する
public sealed class CanvasTransitionRow {

    private readonly CanvasTransitionTable table;
    private readonly int row;

    internal CanvasTransitionRow(CanvasTransitionTable table, int row) {
        this.table = table;
        this.row = row;
    }

    public Entity this[int column] {
        get => table[row, column];
        set => table[row, column] = value;
    }
}
