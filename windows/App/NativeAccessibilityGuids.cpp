// The SDK declares annotation GUIDs, but its UUID/OleAcc import libraries do
// not define them. Instantiate the SDK definitions once per native executable.
#include <windows.h>
#include <initguid.h>
#include <oleacc.h>
#include <UIAutomationCore.h>
#include <UIAutomationCoreApi.h>
