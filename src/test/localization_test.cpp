#include <base/str.h>

#include <game/localization.h>

#include <gtest/gtest.h>

#include <array>
#include <string>

static const std::array<const char *, 15> gs_apStrings = {
	"zero", "one", "two", "three", "four", "five", "six", "seven",
	"eight", "nine", "ten", "eleven", "twelve", "thirteen", "fourteen"};

static const char *Find(const CLocalizationDatabase &Db, const char *pStr, const char *pContext = "")
{
	return Db.FindString(str_quickhash(pStr), str_quickhash(pContext));
}

// AddString must keep m_vStrings sorted so that the binary search in FindString
// works, even though AddString is called before Load sorts the vector. Adding
// entries whose hashes are not in ascending order used to call std::equal_range
// on an unsorted range, which is undefined behaviour and aborts when the
// libstdc++ debug mode (enabled in Debug builds) is used.
TEST(Localization, AddStringKeepsSorted)
{
	CLocalizationDatabase Db;
	for(const char *pStr : gs_apStrings)
		Db.AddString(pStr, pStr, "");
	for(const char *pStr : gs_apStrings)
		EXPECT_STREQ(Find(Db, pStr), pStr);
}

// Adding the same original string twice must not create a duplicate, otherwise
// FindString's check for a unique match fails and the translation is lost.
TEST(Localization, AddStringNoDuplicates)
{
	CLocalizationDatabase Db;
	for(const char *pStr : gs_apStrings)
	{
		Db.AddString(pStr, pStr, "");
		Db.AddString(pStr, "ignored", "");
	}
	for(const char *pStr : gs_apStrings)
		EXPECT_STREQ(Find(Db, pStr), pStr);
}

// Contexts are part of the lookup key and a missing context falls back to the
// empty ("default") context.
TEST(Localization, Contexts)
{
	CLocalizationDatabase Db;
	for(const char *pStr : gs_apStrings)
	{
		Db.AddString(pStr, "default", "");
		Db.AddString(pStr, "context", "mycontext");
	}
	for(const char *pStr : gs_apStrings)
	{
		EXPECT_STREQ(Find(Db, pStr, "mycontext"), "context");
		EXPECT_STREQ(Find(Db, pStr, ""), "default");
	}
	EXPECT_STREQ(Find(Db, "zero", "othercontext"), "default");
	EXPECT_EQ(Find(Db, "missing"), nullptr);
}

static bool Valid(const char *pSource, const char *pTranslation)
{
	return CLocalizationDatabase::IsValidTranslation(pSource, pTranslation);
}

TEST(Localization, ValidTranslations)
{
	EXPECT_TRUE(Valid("Hello", "Hallo"));
	EXPECT_TRUE(Valid("", ""));
	EXPECT_TRUE(Valid("%s has %d points", "%s hat %d Punkte"));
	EXPECT_TRUE(Valid("%d%%", "%d %%"));
	EXPECT_TRUE(Valid("%5.2f", "%.2f"));
	EXPECT_TRUE(Valid("%-10s|", "%s"));
	EXPECT_TRUE(Valid("%lld", "%lld"));
	EXPECT_TRUE(Valid("%*d", "%*d"));
	EXPECT_TRUE(Valid("%1$s %2$d", "%1$s %2$d"));
	EXPECT_TRUE(Valid("100%", "100%"));
	EXPECT_TRUE(Valid("no format", ""));
}

TEST(Localization, ExtraOrMissingSpecifiers)
{
	EXPECT_FALSE(Valid("Hello", "Hello %s"));
	EXPECT_FALSE(Valid("%s", "%s %s"));
	EXPECT_FALSE(Valid("%s %d", "%s"));
	EXPECT_FALSE(Valid("%d", ""));
	EXPECT_FALSE(Valid("%d", "no number"));
	EXPECT_FALSE(Valid("Hello", "%x"));
}

TEST(Localization, WrongTypeOrOrder)
{
	EXPECT_FALSE(Valid("%s %d", "%d %s"));
	EXPECT_FALSE(Valid("%s has %d points", "%d Punkte fuer %s"));
	EXPECT_FALSE(Valid("%d", "%s"));
	EXPECT_FALSE(Valid("%d", "%ld"));
	EXPECT_FALSE(Valid("%d", "%lld"));
	EXPECT_FALSE(Valid("%f", "%d"));
	EXPECT_FALSE(Valid("%s %d", "%2$s %1$d"));
	EXPECT_FALSE(Valid("%s %d", "%1$s %1$s"));
	EXPECT_FALSE(Valid("%s %d", "%1$s %3$d"));
	EXPECT_FALSE(Valid("%s %d", "%1$s %d"));
	EXPECT_FALSE(Valid("%s", "%1$s %2$s"));
	EXPECT_FALSE(Valid("%s", "%1$s %1$d"));
}

TEST(Localization, NumberedArguments)
{
	EXPECT_TRUE(Valid("%s", "%1$s"));
	EXPECT_TRUE(Valid("%s %d", "%2$d %1$s"));
	EXPECT_TRUE(Valid("%1$s %2$s", "%2$s %1$s"));
	EXPECT_TRUE(Valid("%d of %d servers", "%2$d sunucunun %1$d tanesi"));
	EXPECT_TRUE(Valid("%s %d %s", "%3$s %1$s %2$d"));
}

TEST(Localization, PercentPercent)
{
	EXPECT_FALSE(Valid("100%%", "100%"));
	EXPECT_FALSE(Valid("100%", "100%%"));
	EXPECT_FALSE(Valid("%d%%", "%d%s"));
	EXPECT_FALSE(Valid("Hello", "100% sure"));
	EXPECT_FALSE(Valid("Hello", "100%"));
}

TEST(Localization, StarArguments)
{
	EXPECT_FALSE(Valid("%d", "%*d"));
	EXPECT_FALSE(Valid("%*d", "%d"));
	EXPECT_FALSE(Valid("%s", "%.*s"));
	EXPECT_FALSE(Valid("%.*s", "%s"));
	EXPECT_FALSE(Valid("%*d", "%*.*d"));
	EXPECT_FALSE(Valid("%*d", "%*s"));
}

TEST(Localization, PercentN)
{
	EXPECT_FALSE(Valid("Hello", "%n"));
	EXPECT_FALSE(Valid("%n", "%n"));
	EXPECT_FALSE(Valid("%s", "%s%hhn"));
	EXPECT_FALSE(Valid("%s", "%ln%s"));
	EXPECT_FALSE(Valid("%d", "%5$n %d"));
}

TEST(Localization, Length)
{
	std::string Long(CLocalizationDatabase::MAX_TRANSLATION_LENGTH, 'a');
	EXPECT_TRUE(Valid("a", Long.c_str()));
	Long += 'a';
	EXPECT_FALSE(Valid("a", Long.c_str()));
	std::string LongFormat = std::string(CLocalizationDatabase::MAX_TRANSLATION_LENGTH, ' ') + "%s";
	EXPECT_FALSE(Valid("%s", LongFormat.c_str()));
}

TEST(Localization, ReasonIsReported)
{
	const char *pReason = nullptr;
	EXPECT_FALSE(CLocalizationDatabase::IsValidTranslation("%d", "%s", &pReason));
	EXPECT_NE(pReason, nullptr);
	EXPECT_FALSE(CLocalizationDatabase::IsValidTranslation("%d", "%n", &pReason));
	EXPECT_NE(pReason, nullptr);
	EXPECT_TRUE(CLocalizationDatabase::IsValidTranslation("%d", "%d", &pReason));
	EXPECT_EQ(pReason, nullptr);
}
