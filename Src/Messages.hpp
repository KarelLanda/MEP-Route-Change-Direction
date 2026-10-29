#ifndef MESSAGES_HPP
#define MESSAGES_HPP

#include "UniString.hpp"

namespace OtocSmerTZB::Messages {

// =============================================================================
// Centralizovaná tabulka všech hlášení a textů doplňku OtocSmerTZB
// Strukturováno pro budoucí správu z přehledové Excel tabulky stavů.
// =============================================================================

struct AlertRule {
	const char* code;        // Unikátní kód stavu (pro budoucí mapování v Excel tabulce)
	bool        showPopup;   // Zda zobrazit vyskakovací dialog uživatelům (true = alert, false = jen log)
	const char* title;       // Nadpis upozornění
	const char* text;        // Text zprávy v češtině (dle CZE lokalizace Archicadu)
};

// Název operace pro Zpět / Znovu (Undo / Redo fronta v Archicadu)
inline constexpr const char* kUndoCommandName = "Otočení směru TZB trasy";

// Pravidla jednotlivých hraničních a chybových stavů
inline constexpr AlertRule kNoSelection = {
	"MSG_001_NO_SELECTION",
	true,
	"Otočit směr TZB trasy",
	"Není vybrán žádný prvek. Vyberte prosím jednu nebo více tras či prvků TZB."
};

inline constexpr AlertRule kNoSupportedMepElements = {
	"MSG_002_NO_SUPPORTED_MEP",
	true,
	"Otočit směr TZB trasy",
	"Ve výběru nebyl nalezen žádný podporovaný TZB prvek (Trasa TZB, Flexibilní potrubí nebo 2-portová armatura/tvarovka)."
};

inline constexpr AlertRule kElementLockedOrNotEditable = {
	"MSG_003_LOCKED_OR_NOT_EDITABLE",
	true,
	"Otočit směr TZB trasy",
	"Některé vybrané TZB prvky nelze otočit, protože jsou uzamčené, na uzamčené vrstvě nebo nejsou rezervované v Teamworku."
};

inline constexpr AlertRule kInlineElementNotTwoPort = {
	"MSG_004_INLINE_NOT_TWO_PORT",
	true,
	"Otočit směr TZB trasy",
	"Vybranou armaturu nebo tvarovku nelze otočit, protože nemá právě 2 porty v přímém směru."
};

inline constexpr AlertRule kPartialOrFullFailure = {
	"MSG_005_OPERATION_FAILED",
	true,
	"Otočit směr TZB trasy",
	"Při otáčení některých TZB prvků došlo k chybě. Podrobnosti naleznete v okně Zpráva (Log)."
};

} // namespace OtocSmerTZB::Messages

#endif // MESSAGES_HPP
