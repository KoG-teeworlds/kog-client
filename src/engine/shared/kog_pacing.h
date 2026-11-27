#ifndef ENGINE_SHARED_KOG_PACING_H
#define ENGINE_SHARED_KOG_PACING_H

#include <algorithm>
#include <cstdint>

// When and how often the client draws while nothing needs the full frame rate: the menus, the server browser and the
// settings (no game running), and a window that is unfocused or hidden. The game view does not use this.
//
// The loop asks RenderDue() once per iteration and then waits for events until WakeTime(), so nothing spins.
// Anything that must show up before the next frame calls RequestRedraw() (a window that becomes visible does so by itself).
namespace KogPacing
{
enum class EWindow
{
	FOCUSED,
	UNFOCUSED,
	HIDDEN,
};

constexpr int RATE_FOCUSED = 60;
constexpr int RATE_UNFOCUSED = 30;
// While hidden nothing is drawn; the loop still wakes this often (in milliseconds) to serve the network, jobs and downloads.
constexpr int HIDDEN_WAKE_MS = 250;

class CPacing
{
	int64_t m_NextRender = 0;
	bool m_RedrawRequested = true;
	EWindow m_LastWindow = EWindow::FOCUSED;

public:
	[[nodiscard]] bool RenderDue(int64_t Now, EWindow Window) const
	{
		if(Window == EWindow::HIDDEN)
			return false;
		return m_RedrawRequested || Window != m_LastWindow || Now >= m_NextRender;
	}

	// When the loop has to run again at the latest, in the units of Now.
	[[nodiscard]] int64_t WakeTime(int64_t Now, int64_t Freq, EWindow Window) const
	{
		if(Window == EWindow::HIDDEN)
			return Now + Freq * HIDDEN_WAKE_MS / 1000;
		return RenderDue(Now, Window) ? Now : m_NextRender;
	}

	// Call after a frame was drawn at Now. The next one is due one interval later, on the grid unless a whole interval
	// was missed. MaxRate is a rate the player set (cl_refresh_rate, gfx_refresh_rate), 0 for none; it can only lower the rate.
	void Rendered(int64_t Now, int64_t Freq, EWindow Window, int MaxRate)
	{
		const int Rate = std::max(1, MaxRate > 0 ? std::min(MaxRate, RateOf(Window)) : RateOf(Window));
		m_NextRender = std::max(m_NextRender + Freq / Rate, Now);
		m_RedrawRequested = false;
		m_LastWindow = Window;
	}

	void RequestRedraw() { m_RedrawRequested = true; }

	[[nodiscard]] static int RateOf(EWindow Window) { return Window == EWindow::FOCUSED ? RATE_FOCUSED : RATE_UNFOCUSED; }
};
} // namespace KogPacing

#endif
