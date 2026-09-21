#!/usr/bin/env python
"""Explicit local Windows named-pipe client. No discovery and no send retries."""
import argparse
import ctypes
from ctypes import wintypes as w
import json
import math
import os
import sys
import time
import uuid

MAX_REQUEST = 32 * 1024 * 1024
MAX_RESPONSE = 32 * 1024 * 1024


class ApiError(RuntimeError):
    def __init__(self, error):
        super().__init__(error.get('message', 'API error'))
        self.code = error.get('code')
        self.data = error.get('data')


class TransportError(RuntimeError):
    def __init__(self, message, may_have_been_sent=False):
        super().__init__(message)
        self.may_have_been_sent = may_have_been_sent


class _Overlapped(ctypes.Structure):
    _fields_ = [('Internal', ctypes.c_size_t), ('InternalHigh', ctypes.c_size_t),
                ('Offset', w.DWORD), ('OffsetHigh', w.DWORD), ('hEvent', w.HANDLE)]


def _kernel():
    if os.name != 'nt':
        raise OSError('The named-pipe client requires Windows')
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    definitions = {
        'CreateFileW': ([w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID, w.DWORD, w.DWORD, w.HANDLE], w.HANDLE),
        'CreateEventW': ([w.LPVOID, w.BOOL, w.BOOL, w.LPCWSTR], w.HANDLE),
        'CloseHandle': ([w.HANDLE], w.BOOL),
        'ReadFile': ([w.HANDLE, w.LPVOID, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.POINTER(_Overlapped)], w.BOOL),
        'WriteFile': ([w.HANDLE, w.LPCVOID, w.DWORD, ctypes.POINTER(w.DWORD), ctypes.POINTER(_Overlapped)], w.BOOL),
        'GetOverlappedResult': ([w.HANDLE, ctypes.POINTER(_Overlapped), ctypes.POINTER(w.DWORD), w.BOOL], w.BOOL),
        'CancelIoEx': ([w.HANDLE, ctypes.POINTER(_Overlapped)], w.BOOL),
        'WaitForSingleObject': ([w.HANDLE, w.DWORD], w.DWORD),
        'WaitNamedPipeW': ([w.LPCWSTR, w.DWORD], w.BOOL),
    }
    for name, (args, result) in definitions.items():
        fn = getattr(k, name)
        fn.argtypes, fn.restype = args, result
    return k


class Client:
    def __init__(self, pipe, timeout=5.0):
        prefix = '\\\\.\\pipe\\'
        if (not isinstance(pipe, str) or not pipe.startswith(prefix)
                or not pipe[len(prefix):] or len(pipe) > 240
                or any(ch in pipe[len(prefix):] for ch in '\\/\0')):
            raise ValueError('Use an explicit local \\\\.\\pipe\\name including the target PID')
        if not math.isfinite(timeout) or not 0 < timeout <= 120:
            raise ValueError('timeout must be in (0, 120] seconds')
        self.pipe, self.timeout = pipe, timeout

    def _exchange(self, wire):
        k = _kernel()
        deadline = time.monotonic() + self.timeout
        invalid = ctypes.c_void_p(-1).value
        handle = invalid
        sent = False

        def remaining():
            value = int((deadline - time.monotonic()) * 1000)
            if value <= 0:
                raise TimeoutError('Pipe deadline expired')
            return value

        def transfer(write, buffer, size):
            event = k.CreateEventW(None, True, False, None)
            if not event:
                raise ctypes.WinError(ctypes.get_last_error())
            op = _Overlapped(hEvent=event)
            count = w.DWORD()
            try:
                fn = k.WriteFile if write else k.ReadFile
                if not fn(handle, buffer, size, ctypes.byref(count), ctypes.byref(op)):
                    error = ctypes.get_last_error()
                    if error != 997:  # ERROR_IO_PENDING
                        raise ctypes.WinError(error)
                    try:
                        wait = k.WaitForSingleObject(event, remaining())
                        if wait != 0:
                            raise TimeoutError('Pipe I/O deadline expired')
                    except BaseException:
                        k.CancelIoEx(handle, ctypes.byref(op))
                        # Keep the buffer and OVERLAPPED alive through cancellation.
                        k.GetOverlappedResult(handle, ctypes.byref(op), ctypes.byref(count), True)
                        raise
                    if not k.GetOverlappedResult(handle, ctypes.byref(op), ctypes.byref(count), False):
                        raise ctypes.WinError(ctypes.get_last_error())
                return count.value
            finally:
                k.CloseHandle(event)
        try:
            while True:
                remaining()
                # OVERLAPPED plus anonymous SQOS: never let a server impersonate
                # this client. Retry only connection establishment, before sends.
                handle = k.CreateFileW(self.pipe, 0xC0000000, 0, None, 3,
                                       0x40000000 | 0x00100000, None)
                if handle != invalid:
                    break
                error = ctypes.get_last_error()
                if error != 231:  # ERROR_PIPE_BUSY
                    raise ctypes.WinError(error)
                if not k.WaitNamedPipeW(self.pipe, remaining()):
                    raise ctypes.WinError(ctypes.get_last_error())
            offset = 0
            while offset < len(wire):
                remaining()
                buffer = ctypes.create_string_buffer(wire[offset:])
                sent = True  # failure after this point has an uncertain outcome
                count = transfer(True, buffer, len(wire) - offset)
                if not count:
                    raise OSError('Pipe write made no progress')
                offset += count
            response = bytearray()
            while len(response) < MAX_RESPONSE:
                remaining()
                buffer = ctypes.create_string_buffer(min(65536, MAX_RESPONSE - len(response)))
                count = transfer(False, buffer, len(buffer))
                if not count:
                    raise OSError('Pipe closed without a complete response')
                response.extend(buffer.raw[:count])
                if b'\n' in response:
                    if response.index(b'\n') != len(response) - 1:
                        raise OSError('Expected one newline-terminated response')
                    return bytes(response)
            raise OSError('Response exceeds transport limit')
        except (OSError, TimeoutError) as error:
            suffix = '; outcome unknown: inspect state before retrying' if sent else ''
            raise TransportError(str(error) + suffix, sent) from error
        finally:
            if handle != invalid:
                k.CloseHandle(handle)

    def call(self, method, params=None, request_id=None):
        request_id = request_id if request_id is not None else uuid.uuid4().hex
        if not isinstance(method, str) or not 0 < len(method.encode('utf-8')) <= 80:
            raise ValueError('method must contain 1..80 UTF-8 bytes')
        if not isinstance(request_id, str) or not 0 < len(request_id.encode('utf-8')) <= 80:
            raise ValueError('request_id must contain 1..80 UTF-8 bytes')
        if params is None:
            params = {}
        if not isinstance(params, dict):
            raise ValueError('params must be an object')
        request = {'jsonrpc': '2.0', 'id': request_id, 'method': method, 'params': params}
        wire = (json.dumps(request, ensure_ascii=False, allow_nan=False, separators=(',', ':')) + '\n').encode('utf-8')
        if len(wire) > MAX_REQUEST:
            raise ValueError('Request exceeds 1 MiB')
        # Exactly one exchange. Even transport writes must not be auto-retried.
        raw = self._exchange(wire)
        try:
            reply = json.loads(raw)
            if (not isinstance(reply, dict) or reply.get('jsonrpc') != '2.0'
                    or reply.get('id') != request_id
                    or ('result' in reply) == ('error' in reply)):
                raise ValueError('Invalid response envelope or mismatched id')
        except (ValueError, UnicodeError) as error:
            raise TransportError('Invalid reply; outcome unknown', True) from error
        if 'error' in reply:
            raise ApiError(reply['error'])
        return reply['result']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pipe', required=True, help='Explicit local pipe path printed by the target instance')
    parser.add_argument('--timeout', type=float, default=5)
    parser.add_argument('--id', dest='request_id')
    parser.add_argument('method')
    parser.add_argument('params', nargs='?', default='{}', help='JSON object, or - for stdin')
    args = parser.parse_args()
    try:
        params = json.loads(sys.stdin.read() if args.params == '-' else args.params)
        result = Client(args.pipe, args.timeout).call(args.method, params, args.request_id)
        print(json.dumps(result, ensure_ascii=False, indent=2))
        return 0
    except (ValueError, OSError, ApiError, TransportError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
