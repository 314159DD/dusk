using System.Windows;
using System.Windows.Interop;
using System.Windows.Media.Animation;
using System.Windows.Threading;

namespace Dusk;

public partial class OsdWindow : Window
{
    private readonly DispatcherTimer _timer;
    private IntPtr _hwnd;

    public OsdWindow()
    {
        InitializeComponent();
        _timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(1200) };
        _timer.Tick += (_, _) =>
        {
            _timer.Stop();
            var fade = new DoubleAnimation(0, TimeSpan.FromMilliseconds(200));
            fade.Completed += (_, _) =>
            {
                if (!_timer.IsEnabled) Hide();
            };
            BeginAnimation(OpacityProperty, fade);
        };
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);
        _hwnd = new WindowInteropHelper(this).Handle;
        var style = Native.GetWindowLong(_hwnd, Native.GWL_EXSTYLE);
        Native.SetWindowLong(_hwnd, Native.GWL_EXSTYLE,
            style | Native.WS_EX_NOACTIVATE | Native.WS_EX_TRANSPARENT | Native.WS_EX_TOOLWINDOW);
    }

    internal void Flash(MonitorInfo monitor, string name, string value)
    {
        NameText.Text = name;
        ValueText.Text = value;

        _timer.Stop();
        BeginAnimation(OpacityProperty, null);
        Opacity = 1;
        if (!IsVisible) Show();

        Position(monitor);
        UpdateLayout();
        Position(monitor);

        _timer.Start();
    }

    private void Position(MonitorInfo monitor)
    {
        if (_hwnd == IntPtr.Zero || !Native.GetWindowRect(_hwnd, out var rect)) return;
        var x = monitor.Work.Left + (monitor.Work.Width - rect.Width) / 2;
        var y = monitor.Work.Bottom - rect.Height - monitor.Work.Height / 12;
        Native.SetWindowPos(_hwnd, IntPtr.Zero, x, y, 0, 0,
            Native.SWP_NOSIZE | Native.SWP_NOZORDER | Native.SWP_NOACTIVATE);
    }
}
