"""Inspect and select real Win32 popup menus on an owned private desktop."""
import ctypes
from ctypes import wintypes as w
import time

import private_desktop

user = private_desktop.user
user.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
user.GetMenuItemCount.argtypes = [w.HMENU]
user.GetMenuItemCount.restype = ctypes.c_int
user.GetMenuItemRect.argtypes = [w.HWND, w.HMENU, w.UINT, ctypes.POINTER(w.RECT)]
user.ScreenToClient.argtypes = [w.HWND, ctypes.POINTER(w.POINT)]
user.IsWindow.argtypes = [w.HWND]
user.IsWindowVisible.argtypes = [w.HWND]


class MENUITEMINFO(ctypes.Structure):
    _fields_ = [('cbSize', w.UINT), ('fMask', w.UINT), ('fType', w.UINT),
                ('fState', w.UINT), ('wID', w.UINT), ('hSubMenu', w.HMENU),
                ('hbmpChecked', w.HBITMAP), ('hbmpUnchecked', w.HBITMAP),
                ('dwItemData', ctypes.c_size_t), ('dwTypeData', w.LPWSTR),
                ('cch', w.UINT), ('hbmpItem', w.HBITMAP)]


user.GetMenuItemInfoW.argtypes = [w.HMENU, w.UINT, w.BOOL, ctypes.POINTER(MENUITEMINFO)]


def _items(menu):
    result = []
    count = user.GetMenuItemCount(menu)
    if count < 0:
        raise ctypes.WinError(ctypes.get_last_error())
    for index in range(count):
        label = ctypes.create_unicode_buffer(1024)
        info = MENUITEMINFO(cbSize=ctypes.sizeof(MENUITEMINFO), fMask=0x1 | 0x2 | 0x4 | 0x40 | 0x100,
                            dwTypeData=ctypes.cast(label, w.LPWSTR), cch=len(label))
        private_desktop.check(user.GetMenuItemInfoW(menu, index, True, ctypes.byref(info)))
        result.append(dict(command=info.wID, label=label.value,
                           enabled=not bool(info.fState & 0x3), checked=bool(info.fState & 0x8),
                           children=_items(info.hSubMenu) if info.hSubMenu else [],
                           _menu=menu, _index=index, _submenu=info.hSubMenu,
                           _separator=bool(info.fType & 0x800), _highlighted=bool(info.fState & 0x80)))
    return result


def _popups(test):
    found = []
    @private_desktop.callback
    def visit(hwnd, _):
        pid = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        name = ctypes.create_unicode_buffer(100)
        user.GetClassNameW(hwnd, name, len(name))
        if pid.value == test.pid and name.value == '#32768' and user.IsWindowVisible(hwnd):
            menu = test.desktop.send(hwnd, 0x1E1)  # MN_GETHMENU, the actual popup HMENU.
            if menu:
                found.append((hwnd, menu))
        return True
    private_desktop.check(user.EnumDesktopWindows(test.desktop.desktop, visit, 0))
    return found


def post_context(owner, point=None, source=None):
    """Post WM_CONTEXTMENU; point is screen pixels, None means keyboard target."""
    value = -1 if point is None else (int(point[0]) & 0xFFFF) | ((int(point[1]) & 0xFFFF) << 16)
    private_desktop.check(user.PostMessageW(source or owner, 0x7B, source or owner, value))


def await_menu(test, owner):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        popups = _popups(test)
        if popups:
            return popups[0]
        time.sleep(.01)
    test.fail(f'No native popup appeared for owned HWND {owner}')


class Menu:
    def __init__(self, test, owner, point=None, source=None):
        self.test, self.owner = test, source or owner
        post_context(owner, point, source)
        self.hwnd, self.menu = await_menu(test, owner)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.cancel()

    def items(self):
        def public(items):
            return [dict(command=i['command'], label=i['label'], enabled=i['enabled'],
                         checked=i['checked'], children=public(i['children'])) for i in items]
        return public(_items(self.menu))

    def _closed(self):
        return not any(menu == self.menu for _, menu in _popups(self.test))

    def _wait_closed(self):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self._closed():
                return
            time.sleep(.01)
        self.test.fail('Native context menu did not close')

    def cancel(self):
        if not self._closed():
            # WM_CANCELMODE cancels every nested popup without global input.
            private_desktop.check(user.PostMessageW(self.owner, 0x1F, 0, 0))
            self._wait_closed()

    def _key(self, hwnd, key):
        # Menu tracking consumes queued keys itself, before app dispatch.
        # Posted mouse messages cannot provide MSG.pt on a private desktop.
        private_desktop.check(user.PostMessageW(hwnd, 0x100, key, 1))
        private_desktop.check(user.PostMessageW(hwnd, 0x101, key, 0xC0000001))

    def choose(self, command):
        def find(items, trail):
            for item in items:
                if item['command'] == command and not item['children']:
                    return trail + [item]
                found = find(item['children'], trail + [item])
                if found:
                    return found
            return None
        path = find(_items(self.menu), [])
        self.test.assertTrue(path, f'Command {command} absent from actual native menu')
        self.test.assertTrue(all(item['enabled'] for item in path), f'Command {command} is disabled')
        hwnd = self.hwnd
        for item in path:
            # Native menus may highlight disabled headings. Read MFS_HILITE
            # after every arrow rather than guessing their navigation policy.
            for _ in range(user.GetMenuItemCount(item['_menu']) + 2):
                previous = next((i['_index'] for i in _items(item['_menu']) if i['_highlighted']), None)
                if previous == item['_index']:
                    break
                self._key(hwnd, 0x28)
                deadline = time.monotonic() + 2
                while time.monotonic() < deadline:
                    highlighted = next((i['_index'] for i in _items(item['_menu']) if i['_highlighted']), None)
                    if highlighted is not None and highlighted != previous:
                        break
                    time.sleep(.005)
                else:
                    self.test.fail('Native menu keyboard highlight did not advance')
                if highlighted == item['_index']:
                    break
            else:
                self.test.fail(f'Native menu could not highlight command {command}')
            self._key(hwnd, 0x27 if item['_submenu'] else 0x0D)
            if item['_submenu']:
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    matches = [h for h, menu in _popups(self.test) if menu == item['_submenu']]
                    if matches:
                        hwnd = matches[0]
                        break
                    time.sleep(.01)
                else:
                    self.test.fail('Native context submenu did not open')
        self._wait_closed()
