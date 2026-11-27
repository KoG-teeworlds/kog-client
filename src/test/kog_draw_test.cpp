#include <game/client/kog_draw.h>

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>

using namespace KogDraw;

static void ExpectValid(const SNineSlice &Plan)
{
	for(int i = 0; i < 4; ++i)
	{
		EXPECT_TRUE(std::isfinite(Plan.m_aX[i]));
		EXPECT_TRUE(std::isfinite(Plan.m_aY[i]));
	}
	for(int i = 0; i < 3; ++i)
	{
		EXPECT_LE(Plan.m_aX[i], Plan.m_aX[i + 1]);
		EXPECT_LE(Plan.m_aY[i], Plan.m_aY[i + 1]);
	}
}

TEST(KogDraw, ShadowCoversThePanelAndItsBlur)
{
	const SNineSlice Plan = PlanShadow(100.0f, 50.0f, 200.0f, 80.0f, 10.0f, 2.0f);
	ExpectValid(Plan);
	EXPECT_FLOAT_EQ(Plan.m_aX[0], 90.0f);
	EXPECT_FLOAT_EQ(Plan.m_aX[3], 310.0f);
	EXPECT_FLOAT_EQ(Plan.m_aY[0], 42.0f);
	EXPECT_FLOAT_EQ(Plan.m_aY[3], 142.0f);
}

TEST(KogDraw, ShadowOfAnEmptyOrBrokenPanelIsValid)
{
	const float Nan = std::numeric_limits<float>::quiet_NaN();
	const float Inf = std::numeric_limits<float>::infinity();
	for(const float V : {0.0f, -5.0f, Nan, Inf, -Inf, 1e30f})
	{
		ExpectValid(PlanShadow(V, V, V, V, V, V));
		ExpectValid(PlanShadow(0.0f, 0.0f, V, 10.0f, 18.0f, 5.0f));
	}
}

TEST(KogDraw, ShadowPlanProperty)
{
	std::mt19937 Rng(7);
	std::uniform_real_distribution<float> Position(-5000.0f, 5000.0f);
	std::uniform_real_distribution<float> Size(0.0f, 4000.0f);
	std::uniform_real_distribution<float> Blur(0.0f, 60.0f);
	for(int i = 0; i < 20000; ++i)
	{
		const SNineSlice Plan = PlanShadow(Position(Rng), Position(Rng), Size(Rng), Size(Rng), Blur(Rng), Position(Rng) / 100.0f);
		ExpectValid(Plan);
		// the corner slices never overlap
		EXPECT_LE(Plan.m_aX[1], Plan.m_aX[2]);
		EXPECT_LE(Plan.m_aY[1], Plan.m_aY[2]);
	}
}

TEST(KogDraw, LightenedKeepsAlphaAndStaysInRange)
{
	const ColorRGBA C = Lightened(ColorRGBA(0.1f, 0.2f, 0.3f, 0.5f), 0.07f);
	EXPECT_FLOAT_EQ(C.a, 0.5f);
	EXPECT_GT(C.r, 0.1f);
	EXPECT_LE(C.b, 1.0f);
}
