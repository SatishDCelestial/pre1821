#pragma once

enum SolutionLoadState
{
	slsNone = 100, // only valid at startup
	slsLoading,
	slsWaitingForBackgroundLoadToStart,
	slsBackgroundLoading,
	slsLoadComplete,
	slsReloading,
	slsClosing,
	slsClosed // waiting for slsLoading
};
