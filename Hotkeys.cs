using System.Windows.Input;
using System.Windows.Interop;

namespace Dusk;

internal sealed class Hotkeys : IDisposable
{
    private readonly HwndSource _source;
    private readonly Dictionary<int, string> _actions = new();

    public event Action<string>? Pressed;
    public List<string> Failed { get; } = new();

    public Hotkeys(IReadOnlyDictionary<string, string> bindings)
    {
        var parameters = new HwndSourceParameters("DuskHotkeys")
        {
            ParentWindow = new IntPtr(-3),
            WindowStyle = 0,
        };
        _source = new HwndSource(parameters);
        _source.AddHook(WndProc);

        var id = 1;
        foreach (var (action, keys) in bindings)
        {
            if (string.IsNullOrWhiteSpace(keys)) continue;
            if (TryParse(keys, out var modifiers, out var vk) &&
                Native.RegisterHotKey(_source.Handle, id, modifiers, vk))
            {
                _actions[id] = action;
            }
            else
            {
                Failed.Add(keys);
            }
            id++;
        }
    }

    private static bool TryParse(string text, out uint modifiers, out uint vk)
    {
        modifiers = 0;
        vk = 0;
        foreach (var raw in text.Split('+', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            switch (raw.ToLowerInvariant())
            {
                case "alt": modifiers |= 0x1; break;
                case "ctrl":
                case "control": modifiers |= 0x2; break;
                case "shift": modifiers |= 0x4; break;
                case "win": modifiers |= 0x8; break;
                default:
                    if (!Enum.TryParse<Key>(raw, true, out var key)) return false;
                    vk = (uint)KeyInterop.VirtualKeyFromKey(key);
                    break;
            }
        }
        return vk != 0;
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == Native.WM_HOTKEY && _actions.TryGetValue(wParam.ToInt32(), out var action))
        {
            handled = true;
            Pressed?.Invoke(action);
        }
        return IntPtr.Zero;
    }

    public void Dispose()
    {
        foreach (var id in _actions.Keys) Native.UnregisterHotKey(_source.Handle, id);
        _source.Dispose();
    }
}
