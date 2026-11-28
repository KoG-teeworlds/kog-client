#ifndef ENGINE_SHARED_KOG_NOTICE_WIRE_H
#define ENGINE_SHARED_KOG_NOTICE_WIRE_H

#include <engine/shared/kog_notices.h>

class CUnpacker;

// The message kog-notice@kog.tw of a KoG server, see docs/KOG-NOTICE.md. Read only after the server agreed to
// the capability notices in the KoG hello. Layout and golden vectors: docs/KOG-NOTICE.md of kog-2.0-server and
// test_vectors/kog-notice.json of the protocol repository.
namespace KogNoticeWire {

enum
{
	MAX_KEY = 40,
	MAX_TEXT = KogNotices::MAX_TEXT_BYTES,
};

struct SNotice
{
	KogNotices::EKind m_Kind = KogNotices::EKind::INFO;
	char m_aKey[MAX_KEY + 1] = "";
	char m_aText[MAX_TEXT + 1] = "";
	int m_Value = 0; // quest progress: the counter; maintenance and tournament: seconds left
	int m_Limit = 0; // quest progress: the target
};

// Reads the fields after the message id: kind, key, text, value, limit. False: not understood (a field is missing or
// the text is empty), nothing is shown. A kind that is not known is an info, negative numbers are 0, key and text are
// cut at their limits, control characters become spaces, fields of a later revision are not read.
[[nodiscard]] bool Unpack(CUnpacker *pUnpacker, SNotice *pNotice);

}

#endif
