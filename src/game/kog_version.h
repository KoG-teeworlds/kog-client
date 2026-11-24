#ifndef GAME_KOG_VERSION_H
#define GAME_KOG_VERSION_H

// Placeholder brand: the only place to rename it. Must not be "TClient" or "DDNet" (zlib condition 2).
#define KOG_CLIENT_NAME "KoG Client"
#define KOG_CLIENT_EXECUTABLE "KoG-Client"
#define KOG_STORAGE_NAME "KoG-Client"

// CMakeLists.txt reads the base version from this line and adds -canary.N or -dev.N for those channels
#ifndef KOG_CLIENT_VERSION
#define KOG_CLIENT_VERSION "0.1.0"
#endif

#ifndef KOG_CLIENT_CHANNEL
#define KOG_CLIENT_CHANNEL "stable"
#endif

// Log in with KoG is on by default in canary and dev builds only (CMake sets it)
#ifndef KOG_LOGIN_DEFAULT
#define KOG_LOGIN_DEFAULT 0
#endif

#define KOG_BASED_ON_TCLIENT "10.9.0"
#define KOG_BASED_ON_DDNET "20.1"

#define KOG_HOME_URL "https://kog.tw/"
#define KOG_WEBSITE_URL "https://client.kog.tw/"
#define KOG_REPOSITORY_URL "https://github.com/KoG-teeworlds/kog-client"
#define KOG_UPSTREAM_TCLIENT_URL "https://github.com/TaterClient/TClient"

#define KOG_UPDATE_BASE_URL "https://client.kog.tw"
#define KOG_UPDATE_FALLBACK_URL "https://raw.githubusercontent.com/KoG-teeworlds/kog-client-updates/published"

#endif
