#include <game/client/kog_motion.h>

#include <gtest/gtest.h>

using namespace KogMotion;

TEST(KogMotion, ANewWidgetStartsAtItsTarget)
{
	CHoverTable Table;
	int A = 0, B = 0;
	EXPECT_FLOAT_EQ(Table.Update(&A, false, 1.0, 0.1f), 0.0f);
	EXPECT_FLOAT_EQ(Table.Update(&B, true, 1.0, 0.1f), 1.0f);
}

TEST(KogMotion, HoverEasesInAndOutWithinTheDuration)
{
	CHoverTable Table;
	int A = 0;
	Table.Update(&A, false, 0.0, 0.1f);
	const float Early = Table.Update(&A, true, 0.02, 0.1f);
	EXPECT_GT(Early, 0.0f);
	EXPECT_LT(Early, 1.0f);
	EXPECT_FLOAT_EQ(Table.Update(&A, true, 0.2, 0.1f), 1.0f);
	const float Out = Table.Update(&A, false, 0.22, 0.1f);
	EXPECT_LT(Out, 1.0f);
	EXPECT_GT(Out, 0.0f);
	EXPECT_FLOAT_EQ(Table.Update(&A, false, 0.4, 0.1f), 0.0f);
}

TEST(KogMotion, TheSameTimeGivesTheSameValue)
{
	CHoverTable Table;
	int A = 0;
	Table.Update(&A, false, 0.0, 0.1f);
	const float First = Table.Update(&A, true, 0.03, 0.1f);
	EXPECT_FLOAT_EQ(Table.Update(&A, true, 0.03, 0.1f), First);
}

TEST(KogMotion, ReducedMotionJumpsAndAHugeGapJumps)
{
	CHoverTable Table;
	int A = 0;
	Table.Update(&A, false, 0.0, 0.1f);
	EXPECT_FLOAT_EQ(Table.Update(&A, true, 0.001, 0.0f), 1.0f);
	EXPECT_FLOAT_EQ(Table.Update(&A, false, 500.0, 0.1f), 0.0f);
}

TEST(KogMotion, ManyWidgetsNeverBreakTheTable)
{
	CHoverTable Table;
	static int s_aIds[5000];
	for(int Round = 0; Round < 3; ++Round)
	{
		for(int i = 0; i < 5000; ++i)
		{
			const float V = Table.Update(&s_aIds[i], i % 2 == 0, Round * 0.01 + i * 1e-6, 0.1f);
			EXPECT_GE(V, 0.0f);
			EXPECT_LE(V, 1.0f);
		}
	}
}
