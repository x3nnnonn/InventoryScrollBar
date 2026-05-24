#pragma once

#include <cstdint>

namespace Dumper7Runtime
{
	struct ResolvedOffsets
	{
		int32_t GObjects = 0;
		int32_t AppendString = 0;
		int32_t GNames = 0;
		int32_t GWorld = 0;
		int32_t ProcessEvent = 0;
		int32_t ProcessEventIdx = 0;
		int32_t GetNameEntryFromName = 0;
		bool    bHasGetNameEntryFromName = false;
	};

	bool InitEngineCore();
	bool Initialize(ResolvedOffsets& out);
}
