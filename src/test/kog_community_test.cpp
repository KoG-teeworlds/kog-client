#include <engine/shared/kog_community.h>

#include <gtest/gtest.h>

using namespace KogCommunity;

static SState Fresh()
{
	return SState{true, true, true, true, true, false};
}

TEST(KogCommunity, FreshInstallSelectsKog)
{
	EXPECT_EQ(PickDefault(Fresh()), EDefault::KOG);
}

TEST(KogCommunity, ReturningPlayerKeepsSelection)
{
	SState State = Fresh();
	State.m_FirstStart = false;
	EXPECT_EQ(PickDefault(State), EDefault::KEEP);
	State.m_InfoAvailable = false;
	EXPECT_EQ(PickDefault(State), EDefault::KEEP);
}

TEST(KogCommunity, WaitsForTheList)
{
	SState State = Fresh();
	State.m_InfoAvailable = false;
	State.m_HasKog = false;
	State.m_HasDDNet = false;
	EXPECT_EQ(PickDefault(State), EDefault::WAIT);
}

TEST(KogCommunity, MissingKogFallsBackToDDNet)
{
	SState State = Fresh();
	State.m_HasKog = false;
	EXPECT_EQ(PickDefault(State), EDefault::DDNET);
	State.m_HasDDNet = false;
	EXPECT_EQ(PickDefault(State), EDefault::KEEP);
}

TEST(KogCommunity, KillSwitchRestoresOldDefault)
{
	SState State = Fresh();
	State.m_SwitchOn = false;
	EXPECT_EQ(PickDefault(State), EDefault::DDNET);
}

TEST(KogCommunity, TouchedSelectionIsKept)
{
	SState State = Fresh();
	State.m_Touched = true;
	EXPECT_EQ(PickDefault(State), EDefault::KEEP);
}
