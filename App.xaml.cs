using System.Windows;
using System.Windows.Threading;
using Microsoft.Win32;
using Drawing = System.Drawing;
using Forms = System.Windows.Forms;

namespace Dusk;

internal static class Autostart
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string ValueName = "Dusk";

    public static bool IsEnabled
    {
        get
        {
            using var key = Registry.CurrentUser.OpenSubKey(RunKey);
            return key?.GetValue(ValueName) != null;
        }
    }

    public static void Set(bool enabled)
    {
        using var key = Registry.CurrentUser.OpenSubKey(RunKey, writable: true);
        if (key == null) return;
        if (enabled) key.SetValue(ValueName, $"\"{Environment.ProcessPath}\" /tray");
        else key.DeleteValue(ValueName, throwOnMissingValue: false);
    }
}

public partial class App : Application
{
    private Mutex? _mutex;
    private Controller _controller = null!;
    private Hotkeys? _hotkeys;
    private Forms.NotifyIcon? _tray;
    private PanelWindow? _panel;
    private OsdWindow? _osd;
    private DispatcherTimer? _keepAlive;
    private bool _started;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        if (e.Args.Length == 2 && e.Args[0] == "/selftest")
        {
            SelfTest(e.Args[1]);
            return;
        }

        _mutex =new Mutex(true, "Dusk.SingleInstance", out var first);
        if (!first)
        {
            Shutdown();
            return;
        }
        _started = true;

        _controller = new Controller();
        _controller.Refresh();
        _controller.ApplyAll();

        _hotkeys = new Hotkeys(_controller.Settings.Hotkeys);
        _hotkeys.Pressed += OnHotkey;

        _osd = new OsdWindow();
        _panel = new PanelWindow(_controller, _hotkeys.Failed, Quit);

        _tray = new Forms.NotifyIcon { Icon = BuildIcon(), Text = "Dusk", Visible = true };
        _tray.MouseClick += (_, args) =>
        {
            if (args.Button == Forms.MouseButtons.Left) TogglePanel();
        };
        var menu = new Forms.ContextMenuStrip();
        menu.Items.Add("Open", null, (_, _) => _panel.ShowPanel());
        menu.Items.Add("Quit", null, (_, _) => Quit());
        _tray.ContextMenuStrip = menu;

        _keepAlive = new DispatcherTimer { Interval = TimeSpan.FromSeconds(3) };
        _keepAlive.Tick += (_, _) => _controller.ApplyAll();
        _keepAlive.Start();

        SystemEvents.DisplaySettingsChanged += OnDisplayChanged;
        SystemEvents.PowerModeChanged += OnPowerChanged;

        if (!e.Args.Contains("/tray")) _panel.ShowPanel();
    }

    private void OnDisplayChanged(object? sender, EventArgs e) => Dispatcher.BeginInvoke(() =>
    {
        _controller.Refresh();
        _controller.ApplyAll();
        _panel?.Rebuild();
    });

    private void OnPowerChanged(object? sender, PowerModeChangedEventArgs e)
    {
        if (e.Mode == PowerModes.Resume) Dispatcher.BeginInvoke(() => _controller.ApplyAll());
    }

    private void TogglePanel()
    {
        if (_panel == null) return;
        if (_panel.IsVisible)
        {
            _panel.HidePanel();
            return;
        }
        if ((DateTime.UtcNow - _panel.LastHidden).TotalMilliseconds < 300) return;
        _panel.ShowPanel();
    }

    private void OnHotkey(string action)
    {
        var current = _controller.MonitorUnderCursor();
        if (current == null) return;

        if (action == "Toggle")
        {
            _controller.Enabled = !_controller.Enabled;
            _osd?.Flash(current, "All screens", _controller.Enabled ? "On" : "Paused");
            return;
        }

        var all = action.StartsWith("All", StringComparison.Ordinal);
        var step = all ? action[3..] : action;
        var targets = all ? _controller.Monitors : new List<MonitorInfo> { current };
        var settings = _controller.Settings;

        foreach (var monitor in targets)
        {
            var value = _controller.Get(monitor);
            switch (step)
            {
                case "BrightnessUp": _controller.Set(monitor, null, value.Brightness + settings.BrightnessStep); break;
                case "BrightnessDown": _controller.Set(monitor, null, value.Brightness - settings.BrightnessStep); break;
                case "Cooler": _controller.Set(monitor, value.Kelvin + settings.KelvinStep, null); break;
                case "Warmer": _controller.Set(monitor, value.Kelvin - settings.KelvinStep, null); break;
            }
        }

        var shown = _controller.Get(current);
        _osd?.Flash(current, all ? "All screens" : current.Name, $"{shown.Kelvin} K   {shown.Brightness} %");
    }

    private void SelfTest(string folder)
    {
        System.IO.Directory.CreateDirectory(folder);
        var controller = new Controller();
        controller.Refresh();

        var lines = new List<string>();
        foreach (var monitor in controller.Monitors)
        {
            var ramp = new ushort[768];
            var hdc = Native.CreateDC(null, monitor.DeviceName, null, IntPtr.Zero);
            var read = hdc != IntPtr.Zero && Native.GetDeviceGammaRamp(hdc, ramp);
            var write = read && Native.SetDeviceGammaRamp(hdc, ramp);
            if (hdc != IntPtr.Zero) Native.DeleteDC(hdc);
            var (r, g, b) = ColorMath.FromKelvin(3400);
            lines.Add($"{monitor.Name} | {monitor.DeviceName} | {monitor.Bounds.Width}x{monitor.Bounds.Height} | read={read} write={write} | id={monitor.Id}");
            lines.Add($"  3400K -> {r:F3} {g:F3} {b:F3}");
        }
        System.IO.File.WriteAllLines(System.IO.Path.Combine(folder, "probe.txt"), lines);

        var panel = new PanelWindow(controller, Array.Empty<string>(), () => { });
        panel.ShowActivated = false;
        panel.AutoPlace = false;
        panel.Left = -20000;
        panel.Top = -20000;
        panel.Show();
        var settle = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(800) };
        settle.Tick += (_, _) =>
        {
            settle.Stop();
            var scale = 2.0;
            var bitmap = new System.Windows.Media.Imaging.RenderTargetBitmap(
                (int)(panel.ActualWidth * scale), (int)(panel.ActualHeight * scale), 96 * scale, 96 * scale,
                System.Windows.Media.PixelFormats.Pbgra32);
            bitmap.Render(panel);
            var encoder = new System.Windows.Media.Imaging.PngBitmapEncoder();
            encoder.Frames.Add(System.Windows.Media.Imaging.BitmapFrame.Create(bitmap));
            using (var stream = System.IO.File.Create(System.IO.Path.Combine(folder, "panel.png")))
                encoder.Save(stream);
            Shutdown();
        };
        settle.Start();
    }

    private void Quit()
    {
        Shutdown();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        if (_started)
        {
            SystemEvents.DisplaySettingsChanged -= OnDisplayChanged;
            SystemEvents.PowerModeChanged -= OnPowerChanged;
            _keepAlive?.Stop();
            _hotkeys?.Dispose();
            _controller.Flush();
            _controller.RestoreAll();
            if (_tray != null)
            {
                _tray.Visible = false;
                _tray.Dispose();
            }
        }
        _mutex?.Dispose();
        base.OnExit(e);
    }

    private static Drawing.Icon BuildIcon()
    {
        using var stream = typeof(App).Assembly.GetManifestResourceStream("Dusk.icon.ico")!;
        return new Drawing.Icon(stream, Forms.SystemInformation.SmallIconSize);
    }
}
