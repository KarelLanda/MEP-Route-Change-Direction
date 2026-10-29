#ifndef ROUTING_ELEMENT_REVERSER_HPP
#define ROUTING_ELEMENT_REVERSER_HPP

#include "ACAPinc.h"
#include "API_Guid.hpp"
#include "ACAPI/MEPUniqueID.hpp"
#include "ACAPI/MEPEnums.hpp"

namespace OtocSmerTZB {

class RoutingElementReverser {
public:
	// Otočí směr segmentované TZB trasy (Potrubí, Vzduchotechnika, Kabelový žlab).
	// Musí být voláno uvnitř otevřené Undo transakce (ACAPI_CallUndoableCommand).
	// Vrací API_Guid nově vytvořené/upravené trasy (shodné s původním GUID), nebo APINULLGuid při chybě.
	static API_Guid Reverse (const API_Guid& routeGuid);

	// Pomocná funkce: pokud uživatel vybral podprvek trasy (RigidSegment, Elbow, Transition, RoutingSegment, RoutingNode),
	// vrátí GUID nadřazeného RoutingElementu. Jinak vrátí APINULLGuid.
	static API_Guid ResolveParentRoutingElement (const API_Guid& elemGuid, const API_Guid& classId);

	// Zrcadlí kotevní bod průřezu (Levý <-> Pravý, a u ryze svislých segmentů i Horní <-> Dolní) při otočení směru trasy o 180°,
	// aby fyzická poloha potrubí/žlabu v 3D prostoru zůstala identická.
	static API_AnchorID MirrorAnchorLeftRight (API_AnchorID anchor);
	static API_AnchorID MirrorAnchorTopBottom (API_AnchorID anchor);
	static ACAPI::MEP::PreferredTransitionPlacement InvertTransitionPlacement (ACAPI::MEP::PreferredTransitionPlacement placement);
};

} // namespace OtocSmerTZB

#endif // ROUTING_ELEMENT_REVERSER_HPP
