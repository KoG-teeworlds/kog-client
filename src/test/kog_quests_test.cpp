#include <engine/shared/kog_quests.h>

#include <gtest/gtest.h>

using namespace KogQuests;

TEST(KogQuests, TheThreeExamplesOfTheServerDocument)
{
	SQuest Quest;
	ASSERT_TRUE(ParseLine("kog_quest team8 team \"Team of 8\" \"Finish in a team of at least 8\" team_size >= 8", &Quest));
	EXPECT_EQ(Quest.m_Id, "team8");
	EXPECT_TRUE(Quest.m_Team);
	EXPECT_EQ(Quest.m_Name, "Team of 8");
	EXPECT_EQ(Quest.m_Description, "Finish in a team of at least 8");
	EXPECT_EQ(Quest.m_NumConditions, 1);

	ASSERT_TRUE(ParseLine("kog_quest nohammer individual \"No hammer\" \"Finish without using the hammer\" weapon_fired:0 == 0", &Quest));
	EXPECT_FALSE(Quest.m_Team);
	ASSERT_TRUE(ParseLine("kog_quest steady individual \"Cool head\" \"Never be frozen for more than 5 seconds\" freeze_max <= 5", &Quest));
	EXPECT_EQ(Quest.m_Name, "Cool head");
}

TEST(KogQuests, SeveralConditionsAndDecimals)
{
	SQuest Quest;
	ASSERT_TRUE(ParseLine("kog_quest both individual \"Both\" \"Two rules\" freeze_max <= 2.5 weapon_fired:4 > 0", &Quest));
	EXPECT_EQ(Quest.m_NumConditions, 2);
}

TEST(KogQuests, OtherLinesAreNotQuests)
{
	SQuest Quest;
	EXPECT_FALSE(ParseLine("tune_zone 1 gravity 0.5", &Quest));
	EXPECT_FALSE(ParseLine("", &Quest));
	EXPECT_FALSE(ParseLine("kog_questions a b", &Quest));
}

TEST(KogQuests, LinesTheServerRefusesAreNotListed)
{
	SQuest Quest;
	// id: empty, upper case, too long
	EXPECT_FALSE(ParseLine("kog_quest Big individual \"N\" \"D\" team_size >= 1", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa individual \"N\" \"D\" team_size >= 1", &Quest));
	// scope
	EXPECT_FALSE(ParseLine("kog_quest a solo \"N\" \"D\" team_size >= 1", &Quest));
	// unknown stat, weapon id out of range, a param on a stat that has none, bad operator, bad number
	EXPECT_FALSE(ParseLine("kog_quest a individual \"N\" \"D\" speed >= 1", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest a individual \"N\" \"D\" weapon_fired:6 == 0", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest a individual \"N\" \"D\" team_size:1 >= 1", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest a individual \"N\" \"D\" team_size => 1", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest a individual \"N\" \"D\" team_size >= x", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest a individual \"N\" \"D\" team_size >= 1.2.3", &Quest));
	// no condition, missing quotes, empty name
	EXPECT_FALSE(ParseLine("kog_quest a individual \"N\" \"D\"", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest a individual N D team_size >= 1", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest a individual \"\" \"D\" team_size >= 1", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest a individual \"N\" \"unterminated team_size >= 1", &Quest));
}

TEST(KogQuests, LengthLimitsAreInBytes)
{
	SQuest Quest;
	const std::string Name48(48, 'n');
	const std::string Name49(49, 'n');
	EXPECT_TRUE(ParseLine(("kog_quest a individual \"" + Name48 + "\" \"D\" team_size >= 1").c_str(), &Quest));
	EXPECT_FALSE(ParseLine(("kog_quest a individual \"" + Name49 + "\" \"D\" team_size >= 1").c_str(), &Quest));
	const std::string Description161(161, 'd');
	EXPECT_FALSE(ParseLine(("kog_quest a individual \"N\" \"" + Description161 + "\" team_size >= 1").c_str(), &Quest));
	// not valid UTF-8, control character
	EXPECT_FALSE(ParseLine("kog_quest a individual \"\xff\xfe\" \"D\" team_size >= 1", &Quest));
	EXPECT_FALSE(ParseLine("kog_quest a individual \"N\x01\" \"D\" team_size >= 1", &Quest));
	EXPECT_TRUE(ParseLine("kog_quest a individual \"K\xc3\xb6nig\" \"D\" team_size >= 1", &Quest));
}

TEST(KogQuests, AtMostEightConditions)
{
	SQuest Quest;
	std::string Line = "kog_quest a individual \"N\" \"D\"";
	for(int i = 0; i < 8; i++)
		Line += " team_size >= 1";
	EXPECT_TRUE(ParseLine(Line.c_str(), &Quest));
	Line += " team_size >= 1";
	EXPECT_FALSE(ParseLine(Line.c_str(), &Quest));
}

TEST(KogQuests, ListRefusesDuplicatesAndMoreThan32)
{
	CList List;
	EXPECT_TRUE(List.AddLine("kog_quest a individual \"N\" \"D\" team_size >= 1"));
	EXPECT_FALSE(List.AddLine("kog_quest a team \"Other\" \"D\" team_size >= 2"));
	EXPECT_EQ(List.Quests().size(), 1u);
	for(int i = 0; i < 40; i++)
	{
		char aLine[128];
		str_format(aLine, sizeof(aLine), "kog_quest q%d individual \"N\" \"D\" team_size >= 1", i);
		List.AddLine(aLine);
	}
	EXPECT_EQ(List.Quests().size(), (size_t)MAX_QUESTS);
	List.Clear();
	EXPECT_TRUE(List.Empty());
}
