#include <engine/shared/kog_failsafe.h>
#include <engine/shared/kog_update.h>

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>

using namespace KogUpdate;

namespace
{
class CFakeFiles : public IFileOps
{
public:
	std::map<std::string, std::string> m_Files;
	std::set<std::string> m_FailRename;

	bool Exists(const std::string &Path) override { return m_Files.count(Path) > 0; }
	bool Rename(const std::string &From, const std::string &To) override
	{
		if(!m_Files.count(From) || m_FailRename.count(To))
			return false;
		m_Files[To] = m_Files[From];
		m_Files.erase(From);
		return true;
	}
	bool Remove(const std::string &Path) override { return m_Files.erase(Path) > 0; }
};

CFakeFiles Installed()
{
	CFakeFiles Files;
	Files.m_Files = {{"DDNet", "client 1"}, {"data/a.png", "a1"}, {"data/old.png", "old"}};
	return Files;
}

void Download(CFakeFiles *pFiles)
{
	pFiles->m_Files["update/DDNet"] = "client 2";
	pFiles->m_Files["update/data/a.png"] = "a2";
	pFiles->m_Files["update/data/new.png"] = "new";
}

std::vector<SChange> Changes()
{
	return {{"DDNet", "update/DDNet", false}, {"data/a.png", "update/data/a.png", false}, {"data/new.png", "update/data/new.png", false}, {"data/old.png", "", true}};
}
}

TEST(KogFailsafe, StateRoundTrip)
{
	SState State;
	State.SetSequence("stable", 7);
	State.SetSequence("canary", 90);
	State.m_PendingVersion = "1.2.3";
	State.m_LastStartedVersion = "1.2.2";
	State.m_StartFailures = 1;
	State.MarkBad("canary", "1.3.0-canary.4");
	State.m_Applied = {"~DDNet", "+data/new.png", "-data/old.png"};
	State.m_LastCheck = 1234;
	State.m_LastResult = "up to date";
	const SState Parsed = SState::Parse(State.Serialize().c_str());
	EXPECT_EQ(Parsed.Sequence("stable"), 7);
	EXPECT_EQ(Parsed.Sequence("canary"), 90);
	EXPECT_EQ(Parsed.Sequence("dev"), 0);
	EXPECT_EQ(Parsed.m_PendingVersion, "1.2.3");
	EXPECT_EQ(Parsed.m_StartFailures, 1);
	EXPECT_TRUE(Parsed.IsBad("canary", "1.3.0-canary.4"));
	EXPECT_FALSE(Parsed.IsBad("stable", "1.3.0-canary.4"));
	EXPECT_EQ(Parsed.m_Applied, State.m_Applied);
	EXPECT_EQ(Parsed.m_LastCheck, 1234);
	EXPECT_EQ(Parsed.m_LastResult, "up to date");

	const SState Garbage = SState::Parse("junk\n=\nsequence.=5\nstart_failures=-5\nstart_failures=abc\napplied=\napplied=x\nbad=a\n\xff\xfe\n");
	EXPECT_EQ(Garbage.m_StartFailures, 0);
	EXPECT_TRUE(Garbage.m_Applied.empty());
	EXPECT_EQ(Garbage.Sequence(""), 0);
	EXPECT_EQ(SState::Parse(nullptr).m_StartFailures, 0);

	SState Many;
	for(int i = 0; i < 200; i++)
		Many.MarkBad("stable", std::to_string(i).c_str());
	EXPECT_EQ(Many.m_BadVersions.size(), MAX_BAD_VERSIONS);
	EXPECT_TRUE(Many.IsBad("stable", "199"));
	EXPECT_FALSE(Many.IsBad("stable", "0"));
}

TEST(KogFailsafe, ApplyKeepsThePreviousFiles)
{
	CFakeFiles Files = Installed();
	Download(&Files);
	SState State;
	ASSERT_TRUE(ApplyUpdate(Files, &State, Changes(), "0.2.0"));
	EXPECT_EQ(Files.m_Files["DDNet"], "client 2");
	EXPECT_EQ(Files.m_Files["data/a.png"], "a2");
	EXPECT_EQ(Files.m_Files["data/new.png"], "new");
	EXPECT_FALSE(Files.Exists("data/old.png"));
	EXPECT_EQ(Files.m_Files["DDNet.kogprev"], "client 1");
	EXPECT_EQ(Files.m_Files["data/a.png.kogprev"], "a1");
	EXPECT_EQ(Files.m_Files["data/old.png.kogprev"], "old");
	EXPECT_FALSE(Files.Exists("data/new.png.kogprev"));
	EXPECT_FALSE(Files.Exists("update/DDNet"));
	EXPECT_EQ(State.m_PendingVersion, "0.2.0");
	EXPECT_EQ(State.m_Applied.size(), 4u);

	Files.m_Files["update/DDNet"] = "client 3";
	Files.m_Files["update/data/a.png"] = "a3";
	ASSERT_TRUE(ApplyUpdate(Files, &State, {{"DDNet", "update/DDNet", false}, {"data/a.png", "update/data/a.png", false}}, "0.3.0"));
	EXPECT_EQ(Files.m_Files["DDNet.kogprev"], "client 2");
	EXPECT_EQ(Files.m_Files["data/a.png.kogprev"], "a2");
	EXPECT_FALSE(Files.Exists("data/old.png.kogprev"));
}

TEST(KogFailsafe, AFailureHalfwayRestoresEverything)
{
	CFakeFiles Files = Installed();
	Download(&Files);
	Files.m_FailRename.insert("data/new.png");
	SState State;
	State.m_PendingVersion = "0.1.0";
	State.m_Applied = {"~data/a.png"};
	Files.m_Files["data/a.png.kogprev"] = "a0";
	EXPECT_FALSE(ApplyUpdate(Files, &State, Changes(), "0.2.0"));
	EXPECT_EQ(Files.m_Files["DDNet"], "client 1");
	EXPECT_EQ(Files.m_Files["data/a.png"], "a1");
	EXPECT_EQ(Files.m_Files["data/old.png"], "old");
	EXPECT_FALSE(Files.Exists("data/new.png"));
	EXPECT_EQ(State.m_PendingVersion, "0.1.0");
	EXPECT_EQ(State.m_Applied.size(), 1u);

	CFakeFiles Locked = Installed();
	Download(&Locked);
	Locked.m_FailRename.insert("DDNet.kogprev");
	SState Fresh;
	EXPECT_FALSE(ApplyUpdate(Locked, &Fresh, Changes(), "0.2.0"));
	EXPECT_EQ(Locked.m_Files["DDNet"], "client 1");
	EXPECT_TRUE(Fresh.m_PendingVersion.empty());
}

TEST(KogFailsafe, ACrashingBuildIsRolledBack)
{
	CFakeFiles Files = Installed();
	Download(&Files);
	SState State;
	State.m_LastStartedVersion = "0.1.0";
	ASSERT_TRUE(ApplyUpdate(Files, &State, Changes(), "0.2.0"));

	bool Marker = false;
	auto Launch = [&](const char *pVersion) {
		const ELaunchAction Action = OnLaunch(&State, Marker, pVersion);
		Marker = true;
		return Action;
	};
	EXPECT_EQ(Launch("0.2.0"), ELaunchAction::CONTINUE);
	EXPECT_EQ(Launch("0.2.0"), ELaunchAction::CONTINUE);
	EXPECT_EQ(State.m_StartFailures, 1);
	ASSERT_EQ(Launch("0.2.0"), ELaunchAction::ROLLBACK);
	RollbackUpdate(Files, &State, "stable");

	EXPECT_EQ(Files.m_Files["DDNet"], "client 1");
	EXPECT_EQ(Files.m_Files["data/a.png"], "a1");
	EXPECT_EQ(Files.m_Files["data/old.png"], "old");
	EXPECT_FALSE(Files.Exists("data/new.png"));
	EXPECT_FALSE(Files.Exists("DDNet.kogprev"));
	EXPECT_TRUE(State.IsBad("stable", "0.2.0"));
	EXPECT_TRUE(State.m_PendingVersion.empty());
	EXPECT_TRUE(State.m_Applied.empty());

	Marker = false;
	EXPECT_EQ(Launch("0.1.0"), ELaunchAction::CONTINUE);
	OnStartupComplete(&State, "0.1.0");
	EXPECT_EQ(State.m_LastStartedVersion, "0.1.0");

	SManifest Manifest;
	const std::string Json = "{\"format\":2,\"channel\":\"stable\",\"sequence\":2,\"issued\":1000000,\"expires\":2000000,\"version\":\"0.2.0\","
				 "\"platforms\":{\"linux-x86_64\":{}}}";
	ASSERT_TRUE(ParseManifest(Json.c_str(), Json.size(), "linux-x86_64", &Manifest, nullptr, 0));
	SDecideInput In;
	In.m_pChannel = "stable";
	In.m_pCurrentVersion = "0.1.0";
	In.m_Now = 1500000;
	In.m_pBadVersion = State.IsBad("stable", "0.2.0") ? "0.2.0" : "";
	EXPECT_EQ(Decide(Manifest, In), EVerdict::KNOWN_BAD);
	const std::string Newer = "{\"format\":2,\"channel\":\"stable\",\"sequence\":3,\"issued\":1000000,\"expires\":2000000,\"version\":\"0.2.1\","
				  "\"platforms\":{\"linux-x86_64\":{}}}";
	ASSERT_TRUE(ParseManifest(Newer.c_str(), Newer.size(), "linux-x86_64", &Manifest, nullptr, 0));
	EXPECT_EQ(Decide(Manifest, In), EVerdict::APPLY);
}

TEST(KogFailsafe, GoodAndFlakyBuildsAreLeftAlone)
{
	CFakeFiles Files = Installed();
	Download(&Files);
	SState State;
	ASSERT_TRUE(ApplyUpdate(Files, &State, Changes(), "0.2.0"));
	EXPECT_EQ(OnLaunch(&State, false, "0.2.0"), ELaunchAction::CONTINUE);
	OnStartupComplete(&State, "0.2.0");
	EXPECT_TRUE(State.m_PendingVersion.empty());
	EXPECT_EQ(Files.m_Files["DDNet.kogprev"], "client 1");

	for(int i = 0; i < 5; i++)
		EXPECT_EQ(OnLaunch(&State, true, "0.2.0"), ELaunchAction::CONTINUE);
	EXPECT_GE(State.m_StartFailures, ROLLBACK_AFTER_FAILED_STARTS);

	SState Flaky;
	Flaky.m_PendingVersion = "0.3.0";
	EXPECT_EQ(OnLaunch(&Flaky, false, "0.3.0"), ELaunchAction::CONTINUE);
	EXPECT_EQ(OnLaunch(&Flaky, true, "0.3.0"), ELaunchAction::CONTINUE);
	OnStartupComplete(&Flaky, "0.3.0");
	EXPECT_EQ(OnLaunch(&Flaky, false, "0.3.0"), ELaunchAction::CONTINUE);
	EXPECT_EQ(Flaky.m_StartFailures, 0);

	SState Other;
	Other.m_PendingVersion = "0.3.0";
	EXPECT_EQ(OnLaunch(&Other, true, "0.1.0"), ELaunchAction::CONTINUE);
	EXPECT_EQ(OnLaunch(&Other, true, "0.1.0"), ELaunchAction::CONTINUE);
	EXPECT_EQ(OnLaunch(&Other, true, "0.1.0"), ELaunchAction::CONTINUE);
}

TEST(KogFailsafe, ConfigIsBackedUpWhenTheVersionChanges)
{
	SState State;
	EXPECT_FALSE(ConfigBackupNeeded(State, "0.1.0"));
	State.m_LastStartedVersion = "0.1.0";
	EXPECT_FALSE(ConfigBackupNeeded(State, "0.1.0"));
	EXPECT_TRUE(ConfigBackupNeeded(State, "0.2.0"));
}
