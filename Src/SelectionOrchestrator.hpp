#ifndef SELECTION_ORCHESTRATOR_HPP
#define SELECTION_ORCHESTRATOR_HPP

#include "ACAPinc.h"

namespace OtocSmerTZB {

class SelectionOrchestrator {
public:
	// Hlavní výkonná funkce volaná po kliknutí na položku menu "Otočit směr TZB trasy"
	static GSErrCode ExecuteOnCurrentSelection ();
};

} // namespace OtocSmerTZB

#endif // SELECTION_ORCHESTRATOR_HPP
