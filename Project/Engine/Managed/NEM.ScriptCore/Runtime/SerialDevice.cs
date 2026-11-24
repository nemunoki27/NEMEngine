using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;

namespace NEMEngine;

public sealed record SerialDeviceInfo(string PortName, string DeviceID) {
    public static IReadOnlyList<SerialDeviceInfo> Enumerate() {
        var devices = new List<SerialDeviceInfo>();
        if (!OperatingSystem.IsWindows()) return devices;
        using var ports = Registry.LocalMachine.OpenSubKey(@"HARDWARE\DEVICEMAP\SERIALCOMM");
        if (ports == null) return devices;
        var ids = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        using var usb = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Enum\USB");
        if (usb != null) {
            foreach (string model in usb.GetSubKeyNames()) {
                using var instances = usb.OpenSubKey(model);
                if (instances == null) continue;
                foreach (string instance in instances.GetSubKeyNames()) {
                    using var settings = instances.OpenSubKey(instance + @"\Device Parameters");
                    if (settings?.GetValue("PortName") is string port)
                        ids[port] = @"USB\" + model + @"\" + instance;
                }
            }
        }
        foreach (string key in ports.GetValueNames()) {
            if (ports.GetValue(key) is string port)
                devices.Add(new(port, ids.GetValueOrDefault(port, "")));
        }
        return devices;
    }
}

// ポートの読み書きはワーカー、受信通知はゲームのUpdate直前に実行する
public sealed class SerialDevice : IDisposable {
    private static SerialDevice[] devices = [];
    private readonly object gate = new();
    private readonly Queue<byte[]> received = new(), outgoing = new();
    private readonly Thread worker;
    private volatile bool closing, connected;
    private volatile string error = "";
    public bool IsConnected => connected;
    public string Error => error;
    public event Action? FrameStarted;
    public event Action? FrameEnded;
    public event Action<ReadOnlyMemory<byte>>? Received;

    public SerialDevice(string portName, int baudRate = 115200, bool dtr = true, bool rts = true) {
        if (!Regex.IsMatch(portName, @"\ACOM[1-9][0-9]*\z", RegexOptions.IgnoreCase))
            throw new ArgumentException("COMポート名を指定してください", nameof(portName));
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(baudRate);
        worker = new Thread(() => Run(portName, baudRate, dtr, rts)) { IsBackground = true, Name = "NEM Serial" };
        devices = [.. devices, this];
        worker.Start();
    }

    public bool TrySend(ReadOnlySpan<byte> data) {
        lock (gate) {
            if (!connected || closing || outgoing.Count >= 64 || data.Length is 0 or > 4096) return false;
            outgoing.Enqueue(data.ToArray());
            return true;
        }
    }

    private void Run(string portName, int baudRate, bool dtr, bool rts) {
        try {
            using var port = WindowsSerial.Open(portName, baudRate, dtr, rts);
            byte[] buffer = new byte[1024];
            connected = true;
            while (!closing) {
                WindowsSerial.Check(WindowsSerial.ReadFile(port, buffer, buffer.Length, out int count, 0));
                byte[]? send;
                lock (gate) {
                    if (count != 0) {
                        if (received.Count >= 256) throw new IOException("シリアル受信バッファーが一杯です");
                        received.Enqueue(buffer[..count]);
                    }
                    outgoing.TryDequeue(out send);
                }
                if (send != null) {
                    WindowsSerial.Check(WindowsSerial.WriteFile(port, send, send.Length, out int written, 0));
                    if (written != send.Length) throw new IOException("シリアル送信がタイムアウトしました");
                }
                if (count == 0 && send == null) Thread.Sleep(3);
            }
        } catch (Exception ex) { error = ex.Message; }
        finally {
            lock (gate) { connected = false; outgoing.Clear(); received.Clear(); }
        }
    }

    internal static void UpdateAll() {
        foreach (var device in devices) {
            try {
                device.FrameStarted?.Invoke();
                for (int i = 0; i < 256 && !device.closing; ++i) {
                    byte[]? bytes;
                    lock (device.gate) { if (!device.received.TryDequeue(out bytes)) break; }
                    device.Received?.Invoke(bytes);
                }
                device.FrameEnded?.Invoke();
            } catch (Exception ex) { NativeApplicationAPI.WriteLog(2, ex.ToString()); }
        }
    }

    internal static void CloseAll() {
        foreach (var device in devices) device.Dispose();
    }

    public void Dispose() {
        if (closing) return;
        closing = true;
        worker.Join();
        devices = Array.FindAll(devices, device => device != this);
        FrameStarted = FrameEnded = null;
        Received = null;
    }
}

internal static class WindowsSerial {
    private const string Kernel = "kernel32.dll";

    [StructLayout(LayoutKind.Sequential)]
    private struct Dcb {
        public uint Length, BaudRate, Flags;
        public ushort Reserved, XonLimit, XoffLimit;
        public byte ByteSize, Parity, StopBits, Xon, Xoff, ErrorChar, Eof, Event;
        public ushort Reserved1;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct Timeouts { public uint Interval, ReadMultiplier, ReadConstant, WriteMultiplier, WriteConstant; }

    [DllImport(Kernel, CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern SafeFileHandle CreateFileW(string path, uint access, uint share, nint security, uint creation, uint flags, nint template);
    [DllImport(Kernel, SetLastError = true)]
    private static extern bool GetCommState(SafeFileHandle port, ref Dcb state);
    [DllImport(Kernel, SetLastError = true)]
    private static extern bool SetCommState(SafeFileHandle port, ref Dcb state);
    [DllImport(Kernel, SetLastError = true)]
    private static extern bool SetCommTimeouts(SafeFileHandle port, ref Timeouts timeouts);
    [DllImport(Kernel, SetLastError = true)]
    internal static extern bool ReadFile(SafeFileHandle port, [Out] byte[] buffer, int length, out int count, nint overlapped);
    [DllImport(Kernel, SetLastError = true)]
    internal static extern bool WriteFile(SafeFileHandle port, byte[] buffer, int length, out int count, nint overlapped);

    internal static void Check(bool ok) {
        if (!ok) throw new Win32Exception(Marshal.GetLastWin32Error());
    }

    internal static SafeFileHandle Open(string name, int baud, bool dtr, bool rts) {
        var port = CreateFileW(@"\\.\" + name, 0xC0000000, 0, 0, 3, 0, 0);
        try {
            Check(!port.IsInvalid);
            var state = new Dcb { Length = (uint)Marshal.SizeOf<Dcb>() };
            Check(GetCommState(port, ref state));
            state.BaudRate = (uint)baud;
            state.ByteSize = 8;
            state.StopBits = state.Parity = 0;
            state.Flags = 1u | (dtr ? 0x10u : 0u) | (rts ? 0x1000u : 0u);
            Check(SetCommState(port, ref state));
            var timeouts = new Timeouts { Interval = uint.MaxValue, WriteConstant = 100 };
            Check(SetCommTimeouts(port, ref timeouts));
            return port;
        } catch { port.Dispose(); throw; }
    }
}
