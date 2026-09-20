#include "common/stdafx.h"
#include <cstdlib>
OPENMPT_NAMESPACE_BEGIN
void AssertHandler(const mpt::source_location &, const char *, const char *)
{
	std::abort();
}
OPENMPT_NAMESPACE_END
