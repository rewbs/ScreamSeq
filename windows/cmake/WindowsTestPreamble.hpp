#pragma once
// Windows RPC exposes a legacy `small` macro. Shared tests legitimately use
// `small` as a variable; keep SDK macros out without rewriting the Mac fixtures.
#include <windows.h>
#include <rpc.h>
#ifdef small
#undef small
#endif
