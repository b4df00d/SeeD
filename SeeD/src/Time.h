#pragma once

#ifdef WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN


#include <time.h>
class Time
{
public:
	Time* instance;
	INT64 startTicks{};
	INT64 frequency{};
	float ticksPerMs{};

	INT64 currentTicks{};
	INT64 previousTicks{};
	INT64 deltaTicks{};

	float deltaSeconds{};

	void On()
	{
		ZoneScoped;
		// Check to see if this system supports high performance timers.
		QueryPerformanceFrequency((LARGE_INTEGER*)& frequency);
		if (frequency == 0)
		{
			return;
		}

		// Find out how many times the frequency counter ticks every millisecond.
		ticksPerMs = (float)(frequency / 1000);

		QueryPerformanceCounter((LARGE_INTEGER*)& startTicks);

		currentTicks = 0;
		deltaTicks = 0;
	}

	void Off()
	{
		ZoneScoped;
	}

	void Update()
	{
		ZoneScoped;

		previousTicks = currentTicks;

		QueryPerformanceCounter((LARGE_INTEGER*)& currentTicks);

		deltaTicks = currentTicks - previousTicks;

		deltaSeconds = deltaTicks / ticksPerMs / 1000.0f;
	}
};