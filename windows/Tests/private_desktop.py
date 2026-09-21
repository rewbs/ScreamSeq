"""Owned inspection process on a never-switched desktop; no global input."""
import ctypes
from ctypes import wintypes as w
import subprocess
import uuid

user = ctypes.WinDLL('user32', use_last_error=True)
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
callback = ctypes.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
class GUI(ctypes.Structure):
    _fields_ = [('cbSize', w.DWORD), ('flags', w.DWORD), ('hwndActive', w.HWND), ('hwndFocus', w.HWND), ('hwndCapture', w.HWND), ('hwndMenuOwner', w.HWND), ('hwndMoveSize', w.HWND), ('hwndCaret', w.HWND), ('rcCaret', w.RECT)]
class STARTUP(ctypes.Structure):
    _fields_ = [('cb', w.DWORD), ('lpReserved', w.LPWSTR), ('lpDesktop', w.LPWSTR), ('lpTitle', w.LPWSTR), ('dwX', w.DWORD), ('dwY', w.DWORD), ('dwXSize', w.DWORD), ('dwYSize', w.DWORD), ('dwXCountChars', w.DWORD), ('dwYCountChars', w.DWORD), ('dwFillAttribute', w.DWORD), ('dwFlags', w.DWORD), ('wShowWindow', w.WORD), ('cbReserved2', w.WORD), ('lpReserved2', ctypes.c_void_p), ('hStdInput', w.HANDLE), ('hStdOutput', w.HANDLE), ('hStdError', w.HANDLE)]
class PROCINFO(ctypes.Structure):
    _fields_ = [('hProcess', w.HANDLE), ('hThread', w.HANDLE), ('dwProcessId', w.DWORD), ('dwThreadId', w.DWORD)]
user.CreateDesktopW.argtypes = [w.LPCWSTR, w.LPCWSTR, w.LPVOID, w.DWORD, w.DWORD, w.LPVOID]
user.CreateDesktopW.restype = w.HANDLE
user.CloseDesktop.argtypes = [w.HANDLE]
user.GetForegroundWindow.restype = w.HWND
user.GetWindowThreadProcessId.argtypes = [w.HWND, ctypes.POINTER(w.DWORD)]
user.GetClassNameW.argtypes = [w.HWND, w.LPWSTR, ctypes.c_int]
user.GetDlgItem.argtypes = [w.HWND, ctypes.c_int]
user.GetDlgItem.restype = w.HWND
user.SendMessageTimeoutW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM, w.UINT, w.UINT, ctypes.POINTER(ctypes.c_size_t)]
user.EnumDesktopWindows.argtypes = [w.HANDLE, callback, w.LPARAM]
user.GetGUIThreadInfo.argtypes = [w.DWORD, ctypes.POINTER(GUI)]
user.GetThreadDesktop.argtypes = [w.DWORD]
user.GetThreadDesktop.restype = w.HANDLE
user.SetThreadDesktop.argtypes = [w.HANDLE]
kernel.CreateProcessW.argtypes = [w.LPCWSTR, w.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, w.BOOL, w.DWORD, ctypes.c_void_p, w.LPCWSTR, ctypes.POINTER(STARTUP), ctypes.POINTER(PROCINFO)]
kernel.GetExitCodeProcess.argtypes = [w.HANDLE, ctypes.POINTER(w.DWORD)]
kernel.TerminateProcess.argtypes = [w.HANDLE, w.UINT]
kernel.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]
kernel.CloseHandle.argtypes = [w.HANDLE]

def check(value):
    if not value:
        raise ctypes.WinError(ctypes.get_last_error())
    return value

class PrivateDesktop:
    def __enter__(self):
        self.clip = user.GetClipboardSequenceNumber()
        self.foreground = user.GetForegroundWindow()
        self.original = user.GetThreadDesktop(kernel.GetCurrentThreadId())
        self.name = 'ScreamSeqTest-' + uuid.uuid4().hex
        self.desktop = check(user.CreateDesktopW(self.name, None, None, 0, 0xF01FF, None))
        try:
            check(user.SetThreadDesktop(self.desktop))
        except BaseException:
            user.CloseDesktop(self.desktop)
            raise
        self.processes = []
        return self
    def launch(self, args):
        startup = STARTUP(cb=ctypes.sizeof(STARTUP), lpDesktop='WinSta0\\' + self.name)
        info = PROCINFO()
        command = ctypes.create_unicode_buffer(subprocess.list2cmdline(args))
        check(kernel.CreateProcessW(args[0], command, None, None, False, 0, None, None, ctypes.byref(startup), ctypes.byref(info)))
        kernel.CloseHandle(info.hThread)
        self.processes.append(info)
        return info.dwProcessId
    def hwnd(self, pid):
        found = []
        @callback
        def visit(hwnd, _):
            owner = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
            name = ctypes.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, 128)
            if owner.value == pid and name.value == 'ScreamSeqWindowsDevelopment':
                found.append(hwnd)
            return True
        check(user.EnumDesktopWindows(self.desktop, visit, 0))
        assert len(found) == 1, found
        return found[0]
    def focus(self, hwnd):
        info = GUI(cbSize=ctypes.sizeof(GUI))
        check(user.GetGUIThreadInfo(user.GetWindowThreadProcessId(hwnd, None), ctypes.byref(info)))
        return info.hwndFocus
    def send(self, hwnd, message, wp=0, lp=0):
        output = ctypes.c_size_t()
        check(user.SendMessageTimeoutW(hwnd, message, wp, lp, 2, 5000, ctypes.byref(output)))
        return output.value
    def __exit__(self, *_):
        for info in self.processes:
            code = w.DWORD()
            if kernel.GetExitCodeProcess(info.hProcess, ctypes.byref(code)) and code.value == 259:
                check(kernel.TerminateProcess(info.hProcess, 0))
            assert kernel.WaitForSingleObject(info.hProcess, 10000) == 0
            kernel.CloseHandle(info.hProcess)
        check(user.SetThreadDesktop(self.original))
        check(user.CloseDesktop(self.desktop))
        assert user.GetClipboardSequenceNumber() == self.clip
        assert user.GetForegroundWindow() == self.foreground
