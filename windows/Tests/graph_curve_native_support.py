"""Actual Graph Curve HWND helpers scoped to the fixture PID/private desktop.

Mix in helpers without inheriting another TestCase's test methods. Capture child
identity when opening it; current dock placement does not identify its owner.
"""
import ctypes
from ctypes import wintypes as w
import time

import private_desktop
import test_graph_editor as legacy


user = private_desktop.user
user.EnumChildWindows.argtypes = [w.HWND, private_desktop.callback, w.LPARAM]
user.GetWindow.argtypes = [w.HWND, w.UINT]
user.GetWindow.restype = w.HWND
user.GetAncestor.argtypes = [w.HWND, w.UINT]
user.GetAncestor.restype = w.HWND
user.IsWindow.argtypes = [w.HWND]
user.GetDpiForWindow.argtypes = [w.HWND]
user.IsWindowVisible.argtypes = [w.HWND]
user.IsWindowEnabled.argtypes = [w.HWND]
user.IsChild.argtypes = [w.HWND, w.HWND]
user.GetWindowRect.argtypes = [w.HWND, ctypes.POINTER(w.RECT)]
user.GetClientRect.argtypes = user.GetWindowRect.argtypes
user.MapWindowPoints.argtypes = [w.HWND, w.HWND, ctypes.POINTER(w.POINT), w.UINT]
user.SetWindowPos.argtypes = [w.HWND, w.HWND, ctypes.c_int, ctypes.c_int,
                            ctypes.c_int, ctypes.c_int, w.UINT]


class GraphCurveNativeMixin:
    def _native_identity(self, hwnd):
        self.assertTrue(user.IsWindow(hwnd), f'Destroyed logical child {hwnd}')
        pid = w.DWORD()
        user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        name = ctypes.create_unicode_buffer(128)
        user.GetClassNameW(hwnd, name, len(name))
        return pid.value, name.value, int(user.GetWindow(hwnd, 4) or 0)

    def _top_level_class(self, expected_class):
        found = set()
        @private_desktop.callback
        def visit(hwnd, _):
            pid, name, _ = self._native_identity(hwnd)
            if pid == self.pid and name == expected_class:
                found.add(int(hwnd))
            return True
        private_desktop.check(user.EnumDesktopWindows(self.desktop.desktop, visit, 0))
        return found

    def _child_roles(self):
        if not hasattr(self, '_logical_native_children'):
            self._logical_native_children = {}
        return self._logical_native_children

    def logical_child(self, role):
        record = self._child_roles().get(role)
        self.assertIsNotNone(record, f'Open {role} through its observed command first')
        self.assertEqual(self._native_identity(record['hwnd']),
                         (self.pid, record['class'], record['creationOwner']), role)
        return record['hwnd']

    def _logical_child_exists(self, role):
        workspace = self.read('workspace.get')
        parent, child = role.split('.')
        snapshot = workspace['envelopeBank'] if parent == 'bank' else workspace['graphCurve']
        key = {'bank': 'envelopeBank', 'formula': 'formulaWorkbench',
               'reference': 'formulaReference'}[child]
        # Uncreated snapshots contain only visible:false. Existing hidden tools
        # retain their full logical snapshot; no HWND is guessed from visibility.
        return len(snapshot.get(key, {})) > 1

    def observe_child_open(self, role, expected_class, parent_hwnd, action):
        """Observe one opening action; never repeat it or select an unrelated HWND.

        Windows normalizes WS_CHILD hwndParent to its top-level parent when
        creating a popup. Keep that creation-time owner after curve relocation.
        parent_hwnd is evaluated lazily: Guide499 can create the curve itself.
        """
        before = self._top_level_class(expected_class)
        roles = self._child_roles()
        cached = roles.get(role)
        if cached and not user.IsWindow(cached['hwnd']):
            del roles[role]
            cached = None
        if cached:
            self.logical_child(role)
        parent = parent_hwnd()
        expected_owner = int(user.GetAncestor(parent, 2)) if parent else None
        if cached is None and self._logical_child_exists(role):
            # Handles opened before this helper may be adopted only when the
            # logical snapshot exists and discovery is unambiguous. Otherwise
            # fail and instrument its actual opening; do not borrow another Bank.
            used = {r['hwnd'] for key, r in roles.items() if key != role}
            matches = [h for h in before if h not in used]
            # Never use the *current* root to disambiguate: the curve may have
            # moved after creating its child, and another panel's Bank may now
            # share Main as owner. Ambiguous existing handles require an observed
            # opening; unique logical presence is necessary for initial adoption.
            self.assertEqual(len(matches), 1, (role, before, matches))
            observed_owner = self._native_identity(matches[0])[2]
            possible_owners = {int(parent or 0)}
            if role.startswith('curve.'):
                possible_owners.add(int(self.desktop.hwnd(self.pid)))
            self.assertIn(observed_owner, possible_owners, role)
            cached = {'hwnd': matches[0], 'class': expected_class,
                      'creationOwner': observed_owner}
            roles[role] = cached
        action()
        after = self._top_level_class(expected_class)
        created = after - before
        if created:
            self.assertEqual(len(created), 1, (role, before, after))
            hwnd = next(iter(created))
            # A clean hidden child may be replaced; a retained live child may not.
            if cached:
                self.assertNotIn(cached['hwnd'], after, (role, cached, created))
            if expected_owner is None:
                parent = parent_hwnd()
                self.assertTrue(parent, 'Guide did not create its curve owner')
                expected_owner = int(user.GetAncestor(parent, 2))
            self.assertEqual(self._native_identity(hwnd),
                             (self.pid, expected_class, expected_owner), role)
            roles[role] = {'hwnd': hwnd, 'class': expected_class,
                           'creationOwner': expected_owner}
        else:
            self.assertIsNotNone(cached, (role, 'No new or previously captured child', before, after))
            self.assertIn(cached['hwnd'], after, role)
            # Do not compare with the curve's *current* root after a dock/float.
            self.logical_child(role)
        self.assertTrue(self._logical_child_exists(role), role)
        return self.logical_child(role)

    def _possible_curve_hwnd(self):
        windows = self.curve_windows()
        self.assertLessEqual(len(windows), 1, windows)
        return windows[0] if windows else None

    def curve_windows(self):
        found, roots = set(), []

        def matches(hwnd):
            pid = w.DWORD()
            user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            name = ctypes.create_unicode_buffer(128)
            user.GetClassNameW(hwnd, name, len(name))
            if pid.value == self.pid and name.value == 'ScreamSeq.GraphCurve':
                found.add(int(hwnd))
            return pid.value == self.pid

        @private_desktop.callback
        def visit_root(hwnd, _):
            if matches(hwnd):
                roots.append(hwnd)
            return True

        @private_desktop.callback
        def visit_child(hwnd, _):
            matches(hwnd)
            return True

        private_desktop.check(user.EnumDesktopWindows(self.desktop.desktop, visit_root, 0))
        # A docked owner is WS_CHILD and absent from EnumDesktopWindows.
        for hwnd in roots:
            user.EnumChildWindows(hwnd, visit_child, 0)
        return sorted(found)

    def curve_hwnd(self):
        found = self.curve_windows()
        self.assertEqual(len(found), 1, found)
        return found[0]

    def state(self):
        return self.read('workspace.get')['graphCurve']

    def curve_idle(self):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            workspace = self.read('workspace.get')
            if not workspace['documentBusy'] and not workspace['graphCurve'].get('pending', False):
                return workspace
            time.sleep(.02)
        self.fail(str(workspace))

    def curve_api_idle(self, control=None):
        # The retained preview is coalesced on a 120 ms timer. Require a
        # bounded quiet interval before a separate API operation; never retry
        # that operation and never mistake -32002 for an editing result.
        deadline = time.monotonic() + 5
        quiet = None
        while time.monotonic() < deadline:
            workspace = self.read('workspace.get')
            def editor_pending(editor):
                return editor.get('pending', False) or any(
                    editor_pending(editor[child]) for child in
                    ('envelopeBank', 'formulaWorkbench')
                    if isinstance(editor.get(child), dict))
            pending = workspace['documentBusy'] or workspace['pendingViewCommands']
            pending |= any(editor_pending(workspace[key]) for key in
                ('graphCurve', 'graphEditor', 'parameterAutomation', 'instrumentEnvelope'))
            # An enabled physical control is part of readiness, not an action
            # retry. A permanently disabled control still fails this bound.
            if control is not None:
                pending |= not (user.IsWindow(control) and user.IsWindowVisible(control)
                                and user.IsWindowEnabled(control))
            if pending:
                quiet = None
            elif quiet is None:
                quiet = time.monotonic()
            elif time.monotonic() - quiet >= .2:
                return workspace
            time.sleep(.02)
        self.fail(str({'workspace': workspace, 'control': control,
                       'enabled': bool(control and user.IsWindowEnabled(control))}))

    def control(self, identifier):
        if not (480 <= identifier <= 499 or 9100 <= identifier <= 9105):
            return legacy.GraphEditorTests.control(self, identifier)
        hwnd = user.GetDlgItem(self.curve_hwnd(), identifier)
        self.assertTrue(hwnd, identifier)
        return hwnd

    def curve_page(self, index):
        self.curve_idle()
        if not self.state()['visible']:
            self.read('workspace.panel', panel='graphCurve', focus=True)
        if self.state()['page'] != ('curve', 'formula', 'tools')[index]:
            button = user.GetDlgItem(self.curve_hwnd(), 9100 + index)
            self.assertTrue(user.IsWindowVisible(button))
            self.assertTrue(user.IsWindowEnabled(button))
            self.desktop.send(button, 0xF5)  # BM_CLICK on retained native tab.
        self.assertEqual(self.state()['page'], ('curve', 'formula', 'tools')[index])

    def curve_canvas(self):
        self.curve_page(0)
        result = self.state()
        self.assertTrue(result['canvasVisible'])
        return result

    def curve_reveal_control(self, identifier):
        self.curve_idle()
        if not self.state()['visible']:
            self.read('workspace.panel', panel='graphCurve', focus=True)
        control = self.control(identifier)
        if not user.IsWindowVisible(control):
            page = 1 if identifier in (485, 498, 499) else 2 if identifier in (480, 490, 491, 497, 9103, 9104, 9105) else 0
            self.curve_page(page)
        self.assertTrue(user.IsWindowVisible(control), f'Hidden curve control {identifier}')
        return control

    def command(self, identifier, notification=0):
        if identifier == 499:
            # Named Guide must also work before a curve owner or source exists.
            self.curve_idle()
            self.observe_child_open('curve.reference', 'ScreamSeq.FormulaReference',
                self._possible_curve_hwnd,
                lambda: self.desktop.send(self.desktop.hwnd(self.pid), 0x111, identifier))
            return
        if not (480 <= identifier <= 499 or 9103 <= identifier <= 9105):
            return legacy.GraphEditorTests.command(self, identifier, notification)
        control = self.curve_reveal_control(identifier)
        self.curve_api_idle(control)
        self.assertTrue(user.IsWindowEnabled(control), f'Disabled curve control {identifier}')
        action = lambda: self.desktop.send(self.curve_hwnd(), 0x111,
                          identifier | (notification << 16), control)
        if identifier in (497, 498):
            role, klass = ('curve.bank', 'ScreamSeq.EnvelopeBank') if identifier == 497 else ('curve.formula', 'ScreamSeq.FormulaWorkbench')
            self.observe_child_open(role, klass, self.curve_hwnd, action)
        else:
            action()
        self.curve_idle()  # Observe completion; never retry a write/action.

    def field(self, identifier, value):
        if not 480 <= identifier <= 499:
            return legacy.GraphEditorTests.field(self, identifier, value)
        control = self.curve_reveal_control(identifier)
        self.assertTrue(user.IsWindowEnabled(control), identifier)
        text = ctypes.create_unicode_buffer(str(value))
        self.desktop.send(control, 0xC, 0, ctypes.addressof(text))

    def select(self, identifier, index):
        if not 480 <= identifier <= 499:
            return legacy.GraphEditorTests.select(self, identifier, index)
        control = self.curve_reveal_control(identifier)
        self.assertTrue(user.IsWindowEnabled(control), identifier)
        self.desktop.send(control, 0x14E, index)
        self.command(identifier, 1)

    def native_point(self, item, hwnd):
        scale = user.GetDpiForWindow(hwnd) / 96
        return round(item['x'] * scale) & 65535 | ((round(item['y'] * scale) & 65535) << 16)

    def point(self, item):
        return self.native_point(item, self.curve_hwnd())

    def mouse(self, message, point, buttons=0):
        # Call curve_canvas() BEFORE reading coordinates; page layout changes
        # the viewport. No implicit page change after coordinates are captured.
        self.assertTrue(self.state()['canvasVisible'])
        self.desktop.send(self.curve_hwnd(), message, buttons, self.point(point))

    def key(self, key):
        hwnd = self.curve_hwnd()
        focused = self.desktop.focus(hwnd)
        self.assertTrue(focused == hwnd or user.IsChild(hwnd, focused),
                        f'Curve key has another owner: {focused}')
        self.desktop.send(focused, 0x100, key)

    def settle(self):
        self.curve_idle()
        self.desktop.send(self.curve_hwnd(), 0x113, 3)  # The owner, not Main timer5.
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            workspace = self.read('workspace.get')
            state = workspace['graphCurve']
            if not workspace['documentBusy'] and not state['pending'] and (state['previewSamples'] or not state['graph']):
                return state
            time.sleep(.02)
        self.fail(str(workspace))

    def resize_curve_client(self, width, height):
        self.read('workspace.panel', panel='graphCurve', placement='float', focus=True)
        hwnd = self.curve_hwnd()
        scale = user.GetDpiForWindow(hwnd) / 96
        outer, client = w.RECT(), w.RECT()
        private_desktop.check(user.GetWindowRect(hwnd, ctypes.byref(outer)))
        private_desktop.check(user.GetClientRect(hwnd, ctypes.byref(client)))
        private_desktop.check(user.SetWindowPos(hwnd, None, 0, 0,
            round(width * scale) + outer.right - outer.left - client.right,
            round(height * scale) + outer.bottom - outer.top - client.bottom, 0x16))
        private_desktop.check(user.GetClientRect(hwnd, ctypes.byref(client)))
        self.assertAlmostEqual(client.right / scale, width, delta=1)
        self.assertAlmostEqual(client.bottom / scale, height, delta=1)

    def assert_curve_page_bounds(self):
        hwnd = self.curve_hwnd()
        client = w.RECT()
        private_desktop.check(user.GetClientRect(hwnd, ctypes.byref(client)))
        controls = self.state()['controls']
        self.assertTrue(controls)
        for item in controls:
            left, top, right, bottom = item['bounds']
            self.assertGreater(right, left, item)
            self.assertGreater(bottom, top, item)
            self.assertGreaterEqual(left, 0, item)
            self.assertGreaterEqual(top, 0, item)
            self.assertLessEqual(right, client.right, item)
            self.assertLessEqual(bottom, client.bottom, item)
        for index, first in enumerate(controls):
            a = first['bounds']
            for second in controls[index + 1:]:
                b = second['bounds']
                self.assertFalse(min(a[2], b[2]) > max(a[0], b[0]) and
                                 min(a[3], b[3]) > max(a[1], b[1]), (first, second))
