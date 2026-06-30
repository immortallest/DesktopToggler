using System;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace DesktopToggler
{
    internal static class Program
    {
        [STAThread]
        private static void Main()
        {
            Application.SetHighDpiMode(HighDpiMode.SystemAware);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new TrayAppContext());
        }
    }

    /// <summary>
    /// برنامه بدون پنجره؛ فقط یک آیکون در System Tray دارد تا بتوان از آن خارج شد.
    /// </summary>
    internal class TrayAppContext : ApplicationContext
    {
        private readonly NotifyIcon _trayIcon;
        private readonly MouseHook _mouseHook;
        private bool _hiddenState = false; // false = حالت عادی (آیکون‌ها نمایش، تسکبار ثابت)

        public TrayAppContext()
        {
            _trayIcon = new NotifyIcon
            {
                Icon = SystemIcons.Application,
                Visible = true,
                Text = "Desktop Toggler (دابل‌کلیک روی فضای خالی دسکتاپ)"
            };

            var menu = new ContextMenuStrip();
            menu.Items.Add("خروج", null, (s, e) => ExitApp());
            _trayIcon.ContextMenuStrip = menu;

            _mouseHook = new MouseHook();
            _mouseHook.DesktopEmptyAreaDoubleClicked += OnDesktopDoubleClicked;
            _mouseHook.Start();
        }

        private void OnDesktopDoubleClicked(object sender, EventArgs e)
        {
            _hiddenState = !_hiddenState;

            // toggle نمایش آیکون‌های دسکتاپ
            DesktopUtils.ToggleDesktopIcons();

            // toggle حالت اتوهاید تسکبار
            DesktopUtils.SetTaskbarAutoHide(_hiddenState);

            _trayIcon.ShowBalloonTip(800,
                "Desktop Toggler",
                _hiddenState ? "آیکون‌ها مخفی شدند / تسکبار اتوهاید شد" : "آیکون‌ها نمایش داده شدند / تسکبار ثابت شد",
                ToolTipIcon.Info);
        }

        private void ExitApp()
        {
            _mouseHook.Stop();
            _trayIcon.Visible = false;
            Application.Exit();
        }
    }

    /// <summary>
    /// هوک سراسری ماوس برای تشخیص دابل‌کلیک دقیقاً روی فضای خالی دسکتاپ
    /// (نه روی آیکون‌ها و نه روی پنجره‌های دیگر مثل File Explorer)
    /// </summary>
    internal class MouseHook
    {
        public event EventHandler DesktopEmptyAreaDoubleClicked;

        private const int WH_MOUSE_LL = 14;
        private const int WM_LBUTTONDOWN = 0x0201;

        private IntPtr _hookId = IntPtr.Zero;
        private LowLevelMouseProc _proc;

        private DateTime _lastClickTime = DateTime.MinValue;
        private Point _lastClickPoint = Point.Empty;

        private delegate IntPtr LowLevelMouseProc(int nCode, IntPtr wParam, IntPtr lParam);

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT { public int x; public int y; }

        [StructLayout(LayoutKind.Sequential)]
        private struct MSLLHOOKSTRUCT
        {
            public POINT pt;
            public uint mouseData;
            public uint flags;
            public uint time;
            public IntPtr dwExtraInfo;
        }

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelMouseProc lpfn, IntPtr hMod, uint dwThreadId);

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern bool UnhookWindowsHookEx(IntPtr hhk);

        [DllImport("user32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern IntPtr CallNextHookEx(IntPtr hhk, int nCode, IntPtr wParam, IntPtr lParam);

        [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
        private static extern IntPtr GetModuleHandle(string lpModuleName);

        public void Start()
        {
            _proc = HookCallback;
            using (var curProcess = System.Diagnostics.Process.GetCurrentProcess())
            using (var curModule = curProcess.MainModule)
            {
                _hookId = SetWindowsHookEx(WH_MOUSE_LL, _proc, GetModuleHandle(curModule.ModuleName), 0);
            }
        }

        public void Stop()
        {
            if (_hookId != IntPtr.Zero)
            {
                UnhookWindowsHookEx(_hookId);
                _hookId = IntPtr.Zero;
            }
        }

        private IntPtr HookCallback(int nCode, IntPtr wParam, IntPtr lParam)
        {
            if (nCode >= 0 && wParam == (IntPtr)WM_LBUTTONDOWN)
            {
                var hookStruct = Marshal.PtrToStructure<MSLLHOOKSTRUCT>(lParam);
                var clickPoint = new Point(hookStruct.pt.x, hookStruct.pt.y);
                var now = DateTime.Now;

                int dblClickTimeMs = (int)NativeMethods.GetDoubleClickTime();
                int maxDistX = NativeMethods.GetSystemMetrics(NativeMethods.SM_CXDOUBLECLK);
                int maxDistY = NativeMethods.GetSystemMetrics(NativeMethods.SM_CYDOUBLECLK);

                bool isDoubleClick =
                    (now - _lastClickTime).TotalMilliseconds <= dblClickTimeMs &&
                    Math.Abs(clickPoint.X - _lastClickPoint.X) <= maxDistX &&
                    Math.Abs(clickPoint.Y - _lastClickPoint.Y) <= maxDistY;

                if (isDoubleClick)
                {
                    // ریست تا سه‌بار کلیک به اشتباه به‌عنوان دو دابل‌کلیک حساب نشود
                    _lastClickTime = DateTime.MinValue;

                    if (DesktopUtils.IsPointOnEmptyDesktopArea(clickPoint))
                    {
                        DesktopEmptyAreaDoubleClicked?.Invoke(this, EventArgs.Empty);
                    }
                }
                else
                {
                    _lastClickTime = now;
                    _lastClickPoint = clickPoint;
                }
            }

            return CallNextHookEx(_hookId, nCode, wParam, lParam);
        }
    }

    internal static class NativeMethods
    {
        public const int SM_CXDOUBLECLK = 36;
        public const int SM_CYDOUBLECLK = 37;

        [DllImport("user32.dll")]
        public static extern int GetSystemMetrics(int nIndex);

        [DllImport("user32.dll")]
        public static extern uint GetDoubleClickTime();
    }

    /// <summary>
    /// توابع کمکی برای تشخیص ناحیه خالی دسکتاپ، toggle آیکون‌ها و toggle اتوهاید تسکبار
    /// </summary>
    internal static class DesktopUtils
    {
        #region Win32 interop

        [StructLayout(LayoutKind.Sequential)]
        private struct POINT { public int X; public int Y; }

        [StructLayout(LayoutKind.Sequential)]
        private struct LVHITTESTINFO
        {
            public POINT pt;
            public uint flags;
            public int iItem;
            public int iSubItem;
            public int iGroup;
        }

        private const uint LVM_FIRST = 0x1000;
        private const uint LVM_HITTEST = LVM_FIRST + 18;
        private const uint LVHT_NOWHERE = 0x0001;
        private const uint LVHT_ONITEM = 0x000E; // ONITEMICON | ONITEMLABEL | ONITEMSTATEICON

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr WindowFromPoint(POINT p);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder lpClassName, int nMaxCount);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr GetParent(IntPtr hWnd);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern bool ScreenToClient(IntPtr hWnd, ref POINT lpPoint);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, ref LVHITTESTINFO lParam);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr FindWindowEx(IntPtr hwndParent, IntPtr hwndChildAfter, string lpszClass, string lpszWindow);

        [DllImport("user32.dll", CharSet = CharSet.Auto)]
        private static extern IntPtr SendMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

        private delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

        [DllImport("user32.dll")]
        private static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);

        [StructLayout(LayoutKind.Sequential)]
        private struct APPBARDATA
        {
            public int cbSize;
            public IntPtr hWnd;
            public uint uCallbackMessage;
            public uint uEdge;
            public RECT rc;
            public IntPtr lParam;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RECT { public int left, top, right, bottom; }

        [DllImport("shell32.dll")]
        private static extern uint SHAppBarMessage(uint dwMessage, ref APPBARDATA pData);

        private const uint ABM_GETSTATE = 0x00000004;
        private const uint ABM_SETSTATE = 0x0000000A;
        private const uint ABS_AUTOHIDE = 0x0000001;
        private const uint ABS_ALWAYSONTOP = 0x0000002;

        private const uint WM_COMMAND = 0x0111;
        // فرمان داخلی شل برای toggle کردن "Show desktop icons"
        private const int TOGGLE_DESKTOP_ICONS_COMMAND = 0x7402;

        #endregion

        /// <summary>
        /// بررسی می‌کند که نقطه داده‌شده روی فضای خالی دسکتاپ است (نه روی آیکون و نه روی پنجره دیگر)
        /// </summary>
        public static bool IsPointOnEmptyDesktopArea(Point screenPoint)
        {
            var pt = new POINT { X = screenPoint.X, Y = screenPoint.Y };
            IntPtr hwnd = WindowFromPoint(pt);
            if (hwnd == IntPtr.Zero) return false;

            var sb = new System.Text.StringBuilder(256);
            GetClassName(hwnd, sb, sb.Capacity);
            string className = sb.ToString();

            // پنجره زیر نشانگر باید لیست‌ویوی دسکتاپ باشد، نه فایل‌منیجر یا چیز دیگر
            if (className != "SysListView32")
                return false;

            // مطمئن شو این لیست‌ویو متعلق به دسکتاپ است (والدش SHELLDLL_DefView است)
            IntPtr parent = GetParent(hwnd);
            var parentClass = new System.Text.StringBuilder(256);
            GetClassName(parent, parentClass, parentClass.Capacity);
            if (parentClass.ToString() != "SHELLDLL_DefView")
                return false;

            // hit-test برای اطمینان از این‌که دقیقاً روی یک آیکون کلیک نشده
            var clientPt = pt;
            ScreenToClient(hwnd, ref clientPt);

            var hitInfo = new LVHITTESTINFO { pt = clientPt };
            SendMessage(hwnd, LVM_HITTEST, IntPtr.Zero, ref hitInfo);

            bool onItem = (hitInfo.flags & LVHT_ONITEM) != 0;
            return !onItem;
        }

        /// <summary>
        /// پیدا کردن هندل SHELLDLL_DefView دسکتاپ (روی برخی نسخه‌های ویندوز داخل یک WorkerW است)
        /// </summary>
        private static IntPtr FindDesktopDefView()
        {
            IntPtr progman = FindWindow("Progman", "Program Manager");
            IntPtr defView = FindWindowEx(progman, IntPtr.Zero, "SHELLDLL_DefView", null);

            if (defView == IntPtr.Zero)
            {
                IntPtr foundWorkerW = IntPtr.Zero;
                EnumWindows((hWnd, lParam) =>
                {
                    IntPtr shellView = FindWindowEx(hWnd, IntPtr.Zero, "SHELLDLL_DefView", null);
                    if (shellView != IntPtr.Zero)
                    {
                        foundWorkerW = shellView;
                        return false; // توقف enum
                    }
                    return true;
                }, IntPtr.Zero);

                defView = foundWorkerW;
            }

            return defView;
        }

        /// <summary>
        /// toggle کردن نمایش آیکون‌های دسکتاپ (همان کاری که راست‌کلیک روی دسکتاپ > View > Show desktop icons انجام می‌دهد)
        /// </summary>
        public static void ToggleDesktopIcons()
        {
            IntPtr defView = FindDesktopDefView();
            if (defView == IntPtr.Zero) return;

            SendMessage(defView, WM_COMMAND, (IntPtr)TOGGLE_DESKTOP_ICONS_COMMAND, IntPtr.Zero);
        }

        /// <summary>
        /// فعال یا غیرفعال کردن حالت Auto-hide تسکبار
        /// </summary>
        public static void SetTaskbarAutoHide(bool autoHide)
        {
            var data = new APPBARDATA();
            data.cbSize = Marshal.SizeOf(data);

            uint state = SHAppBarMessage(ABM_GETSTATE, ref data);

            if (autoHide)
                state |= ABS_AUTOHIDE;
            else
                state &= ~ABS_AUTOHIDE;

            data.lParam = (IntPtr)state;
            SHAppBarMessage(ABM_SETSTATE, ref data);
        }
    }
}
