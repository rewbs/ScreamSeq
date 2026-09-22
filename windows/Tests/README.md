# Windows application qualification

Use a separate build and disposable projects. Preserve running musician sessions,
clipboard, foreground focus and system audio defaults. Run the complete suite
inside an owned private desktop, including legacy tests that launch subprocesses:

```powershell
python -X utf8 windows/Tests/run_isolated.py --log C:/absolute/new-app-test-log.txt
```

Set `SCREAMSEQ_TEST_EXE` to the exact QA executable first. Tests declare their
additional fixture/plugin environment variables in their source. For a focused
run, repeat `--test module.Class.test_method` or `--test module`.
The log path must be absolute and new. The wrapper verifies the child desktop,
propagates test failure and checks foreground/clipboard preservation.

Private-desktop UI tests exercise actual windows and application handlers. They
do not prove visible typography, accessibility, display timing or speaker output.
Do not run ordinary discovery on the musician's desktop: some older fixtures
inherit their Python process desktop instead of creating their own.
