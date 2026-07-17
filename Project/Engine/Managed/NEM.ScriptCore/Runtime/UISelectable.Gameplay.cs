using System;
using System.Collections.Generic;

namespace NEMEngine;

public sealed unsafe partial class UISelectable {

    private const int KeyboardSubmitDevice = 0;
    private const int GamepadSubmitDevice = 1;

    // 決定に使用するキーボード入力
    public IReadOnlyList<KeyCode> SubmitKeys {
        get {
            int[] codes = NativeApi.UISelectableGetSubmitBindings(entity.native, KeyboardSubmitDevice);
            KeyCode[] bindings = new KeyCode[codes.Length];
            for (int i = 0; i < codes.Length; ++i) {
                bindings[i] = (KeyCode)codes[i];
            }
            return bindings;
        }
    }

    // 決定に使用するゲームパッド入力
    public IReadOnlyList<GamepadButton> SubmitGamepadButtons {
        get {
            int[] codes = NativeApi.UISelectableGetSubmitBindings(entity.native, GamepadSubmitDevice);
            GamepadButton[] bindings = new GamepadButton[codes.Length];
            for (int i = 0; i < codes.Length; ++i) {
                bindings[i] = (GamepadButton)codes[i];
            }
            return bindings;
        }
    }

    // キーボード決定入力を置き換える
    public void SetSubmitKeys(params KeyCode[] keys) {
        ArgumentNullException.ThrowIfNull(keys);
        int[] bindings = new int[keys.Length];
        for (int i = 0; i < keys.Length; ++i) {
            bindings[i] = (int)keys[i];
        }
        NativeApi.UISelectableSetSubmitBindingsValue(entity.native, KeyboardSubmitDevice, bindings);
    }

    // ゲームパッド決定入力を置き換える
    public void SetSubmitGamepadButtons(params GamepadButton[] buttons) {
        ArgumentNullException.ThrowIfNull(buttons);
        int[] bindings = new int[buttons.Length];
        for (int i = 0; i < buttons.Length; ++i) {
            bindings[i] = (int)buttons[i];
        }
        NativeApi.UISelectableSetSubmitBindingsValue(entity.native, GamepadSubmitDevice, bindings);
    }

    // キーボード決定入力を追加する
    public void AddSubmitKey(KeyCode key) {
        List<KeyCode> bindings = new(SubmitKeys);
        if (!bindings.Contains(key)) {
            bindings.Add(key);
            SetSubmitKeys(bindings.ToArray());
        }
    }

    // ゲームパッド決定入力を追加する
    public void AddSubmitGamepadButton(GamepadButton button) {
        List<GamepadButton> bindings = new(SubmitGamepadButtons);
        if (!bindings.Contains(button)) {
            bindings.Add(button);
            SetSubmitGamepadButtons(bindings.ToArray());
        }
    }

    // キーボード決定入力を削除する
    public bool RemoveSubmitKey(KeyCode key) {
        List<KeyCode> bindings = new(SubmitKeys);
        if (!bindings.Remove(key)) {
            return false;
        }
        SetSubmitKeys(bindings.ToArray());
        return true;
    }

    // ゲームパッド決定入力を削除する
    public bool RemoveSubmitGamepadButton(GamepadButton button) {
        List<GamepadButton> bindings = new(SubmitGamepadButtons);
        if (!bindings.Remove(button)) {
            return false;
        }
        SetSubmitGamepadButtons(bindings.ToArray());
        return true;
    }

    // キーボード決定入力をすべて削除する
    public void ClearSubmitKeys() {
        SetSubmitKeys(Array.Empty<KeyCode>());
    }

    // ゲームパッド決定入力をすべて削除する
    public void ClearSubmitGamepadButtons() {
        SetSubmitGamepadButtons(Array.Empty<GamepadButton>());
    }
}
