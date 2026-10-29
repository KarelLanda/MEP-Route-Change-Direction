#ifndef LOGGER_HPP
#define LOGGER_HPP

#include "ACAPinc.h"
#include "DGModule.hpp"
#include "Messages.hpp"

#include <cstdio>
#include <ctime>

namespace OtocSmerTZB::Logger {

inline void WriteToDebugFile (const char* text)
{
	FILE* fp = std::fopen ("/Users/karellanda/Library/CloudStorage/GoogleDrive-landakarel66@gmail.com/Můj disk/Archicad/Doplnky Archicad/Testovane doplnky/OtocSmerTZB_debug.log", "a");
	if (fp != nullptr) {
		std::time_t now = std::time (nullptr);
		char timeBuf[32] = {};
		std::strftime (timeBuf, sizeof (timeBuf), "%Y-%m-%d %H:%M:%S", std::localtime (&now));
		std::fprintf (fp, "[%s] %s\n", timeBuf, text);
		std::fclose (fp);
	}
}

// Vývojové trasovací výpisy do okna Zpráva (ACAPI_WriteReport s withDial = false) i do diagnostického souboru
inline void Debug (const GS::UniString& message)
{
	GS::UniString fullMsg = "[OtocSmerTZB] " + message;
	ACAPI_WriteReport (fullMsg, false);
	WriteToDebugFile (fullMsg.ToCStr (0, MaxUSize, CC_UTF8).Get ());
}

inline void DebugFmt (const char* fmt, ...)
{
	char buffer[2048] = {};
	va_list args;
	va_start (args, fmt);
	vsnprintf (buffer, sizeof (buffer), fmt, args);
	va_end (args);

	GS::UniString fullMsg = GS::UniString ("[OtocSmerTZB] ") + GS::UniString (buffer, CC_UTF8);
	ACAPI_WriteReport (fullMsg, false);
	WriteToDebugFile (fullMsg.ToCStr (0, MaxUSize, CC_UTF8).Get ());
}

// Zobrazení uživatelského upozornění podle pravidla z Messages.hpp
inline void ReportAlert (const Messages::AlertRule& rule, const GS::UniString& extraDetail = GS::EmptyUniString)
{
	GS::UniString msgText (rule.text, CC_UTF8);
	if (!extraDetail.IsEmpty ()) {
		msgText.Append ("\n\n");
		msgText.Append (extraDetail);
	}

	// Vždy zapíšeme do diagnostického logu
	GS::UniString logLine = GS::UniString ("[OtocSmerTZB][") + GS::UniString (rule.code, CC_UTF8) + "] " + msgText;
	ACAPI_WriteReport (logLine, false);

	// Pokud má dané pravidlo zapnuté vyskakovací upozornění, zobrazíme nativní DGWarningAlert
	if (rule.showPopup) {
		DGAlert (
			DG_WARNING,
			GS::UniString (rule.title, CC_UTF8),
			msgText,
			GS::EmptyUniString,
			GS::UniString ("OK", CC_UTF8)
		);
	}
}

} // namespace OtocSmerTZB::Logger

#endif // LOGGER_HPP
