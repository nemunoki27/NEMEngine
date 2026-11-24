using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using NEMEngine;

namespace NeecSample;

public sealed class NeecBoard : IDisposable {
    private SerialDevice? device;
    private readonly StringBuilder line = new();
    private bool overflow, identified;
    private long opened, lastHello, lastSample;
    private int distance = -1;

    public sealed class ButtonState {
        public bool IsPressed { get; internal set; }
        public bool WasPressedThisFrame { get; internal set; }
        public bool WasReleasedThisFrame { get; internal set; }
    }

    public ButtonState Switch { get; } = new();
    public bool IsConnected => device?.IsConnected == true && identified && Error.Length == 0;
    private string error = "";
    public string Error => device is { Error.Length: > 0 } ? device.Error : error;

    public static IReadOnlyList<SerialDeviceInfo> FindBoards() =>
        SerialDeviceInfo.Enumerate().Where(info => info.DeviceID.StartsWith(
            @"USB\VID_2341&PID_0058\", StringComparison.OrdinalIgnoreCase)).ToArray();

    public bool Connect(string? portName = null) {
        Dispose();
        error = "";
        if (portName == null) {
            var boards = FindBoards();
            if (boards.Count != 1) {
                error = boards.Count == 0 ? "Nano Everyが見つかりません" : "接続先のCOMポートを指定してください";
                return false;
            }
            portName = boards[0].PortName;
        }
        device = new SerialDevice(portName);
        device.FrameStarted += BeginFrame;
        device.Received += Receive;
        device.FrameEnded += CheckConnection;
        opened = Environment.TickCount64;
        lastHello = opened - 500;
        return true;
    }

    public bool TryGetDistanceMm(out int value) {
        value = IsConnected && Environment.TickCount64 - lastSample <= 300 ? distance : -1;
        return value >= 0;
    }

    public bool SetLedColor(byte r, byte g, byte b) =>
        IsConnected && Send(FormattableString.Invariant($"LED {r} {g} {b}"));

    public bool SetServoAngle(int degrees) =>
        IsConnected && degrees >= 10 && degrees <= 170 && Send(FormattableString.Invariant($"SERVO {degrees}"));

    private bool Send(string text) => device?.TrySend(Encoding.ASCII.GetBytes(text + "\n")) == true;

    private void BeginFrame() {
        Switch.WasPressedThisFrame = Switch.WasReleasedThisFrame = false;
    }

    private void CheckConnection() {
        if (device == null) return;
        if (device.Error.Length != 0) { error = device.Error; Dispose(); return; }
        long now = Environment.TickCount64;
        if (identified && now - lastSample > 2000 || !identified && now - opened > 4000) {
            error = identified ? "基板からの受信が途切れました" : "NEECプロトコル1の応答がありません";
            Dispose();
        } else if (!identified && device.IsConnected && now - lastHello >= 500) {
            Send("HELLO 1");
            lastHello = now;
        }
    }

    internal void Receive(ReadOnlyMemory<byte> bytes) {
        foreach (byte b in bytes.Span) {
            if (b == '\n') {
                if (!overflow) ApplyLine(line.ToString());
                line.Clear();
                overflow = false;
            } else if (b != '\r') {
                if (line.Length < 64) line.Append((char)b);
                else overflow = true;
            }
        }
    }

    private void ApplyLine(string message) {
        if (!identified) {
            if (message is "NEEC 1 0" or "NEEC 1 1") {
                identified = true;
                Switch.IsPressed = message[^1] == '1';
                lastSample = Environment.TickCount64;
            }
        } else if (message is "BUTTON 0" or "BUTTON 1") {
            bool pressed = message[^1] == '1';
            if (pressed != Switch.IsPressed) {
                Switch.IsPressed = pressed;
                Switch.WasPressedThisFrame |= pressed;
                Switch.WasReleasedThisFrame |= !pressed;
            }
        } else if (message.StartsWith("DIST ", StringComparison.Ordinal) &&
            int.TryParse(message.AsSpan(5), out int mm) && mm >= -1 && mm <= 65535) {
            distance = mm;
            lastSample = Environment.TickCount64;
        }
    }

    public void Dispose() {
        device?.Dispose();
        device = null;
        line.Clear();
        overflow = identified = false;
        Switch.IsPressed = Switch.WasPressedThisFrame = Switch.WasReleasedThisFrame = false;
        distance = -1;
    }
}

