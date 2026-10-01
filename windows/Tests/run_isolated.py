"""Run app qualification inside a never-switched desktop, including legacy tests.

Usage: python -X utf8 windows/Tests/run_isolated.py --log ABSOLUTE_NEW_LOG
Optional --test module.Class.test_method may be repeated for a focused run.
"""
from pathlib import Path
import argparse
import contextlib
import ctypes
import sys
import time
import unittest

import private_desktop


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log',type=Path,required=True)
    parser.add_argument('--test',action='append',default=[])
    parser.add_argument('--inner',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args()
    if not args.log.is_absolute() or args.log.exists():parser.error('Use a new absolute log path')
    if args.inner:
        with args.log.open('x',encoding='utf-8',buffering=1) as output, contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            user=private_desktop.user
            user.GetUserObjectInformationW.argtypes=[private_desktop.w.HANDLE,ctypes.c_int,ctypes.c_void_p,private_desktop.w.DWORD,ctypes.POINTER(private_desktop.w.DWORD)]
            desktop=user.GetThreadDesktop(private_desktop.kernel.GetCurrentThreadId())
            name=ctypes.create_unicode_buffer(256);needed=private_desktop.w.DWORD()
            private_desktop.check(user.GetUserObjectInformationW(desktop,2,name,ctypes.sizeof(name),ctypes.byref(needed)))
            if not name.value.startswith('ScreamSeqTest-'):raise RuntimeError('Qualification child is not on the owned private desktop')
            print('Qualification desktop:',name.value)
            loader=unittest.TestLoader()
            suite=loader.loadTestsFromNames(args.test) if args.test else loader.discover(str(Path(__file__).parent),pattern='test_*.py')
            result=unittest.TextTestRunner(verbosity=2,stream=output).run(suite)
        return 0 if result.wasSuccessful() else 1
    command=[sys.executable,'-X','utf8',str(Path(__file__).resolve()),'--inner','--log',str(args.log)]
    for name in args.test:command+=['--test',name]
    with private_desktop.PrivateDesktop() as desktop:
        desktop.launch(command);process=desktop.processes[-1]
        deadline=time.monotonic()+1800
        while private_desktop.kernel.WaitForSingleObject(process.hProcess,1000)==258:
            if time.monotonic()>=deadline:raise TimeoutError('Isolated qualification exceeded 30 minutes')
        result=private_desktop.w.DWORD()
        private_desktop.check(private_desktop.kernel.GetExitCodeProcess(process.hProcess,ctypes.byref(result)))
    print(f'Isolated application tests exited {result.value}; log: {args.log}')
    return result.value


if __name__=='__main__':sys.exit(main())
