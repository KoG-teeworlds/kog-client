#include <engine/http.h>

#include <game/kog_version.h>

#include <gtest/gtest.h>

#include <string>

TEST(KogUserAgent, Format)
{
	const std::string Agent = IHttpRequest::KOG_USER_AGENT_STRING;
	EXPECT_EQ(Agent.rfind(std::string(KOG_CLIENT_EXECUTABLE "/" KOG_CLIENT_VERSION " ("), 0), 0u);
	EXPECT_EQ(Agent.back(), ')');
	EXPECT_NE(Agent.find("; "), std::string::npos);
}

TEST(KogUserAgent, NotDdnet)
{
	EXPECT_EQ(std::string(IHttpRequest::KOG_USER_AGENT_STRING).find("DDNet"), std::string::npos);
}
