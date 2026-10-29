#include "FlexibleSegmentReverser.hpp"
#include "Logger.hpp"

// MEPAPI
#include "ACAPI/MEPAdapter.hpp"
#include "ACAPI/MEPFlexibleSegment.hpp"
#include "ACAPI/MEPFlexibleSegmentDefault.hpp"
#include "ACAPI/MEPPort.hpp"
#include "ACAPI/MEPRoutingElement.hpp"

// GSRoot
#include "Overloaded.hpp"

#include <algorithm>
#include <vector>

using namespace ACAPI::MEP;

namespace OtocSmerTZB {

bool FlexibleSegmentReverser::Reverse (const API_Guid& flexGuid)
{
	Logger::DebugFmt ("Zahajuji otaceni FlexibleSegmentu %s...", APIGuidToString (flexGuid).ToCStr ().Get ());

	ACAPI::Result<FlexibleSegment> flexRes = FlexibleSegment::Get (Adapter::UniqueID (flexGuid));
	if (flexRes.IsErr ()) {
		Logger::DebugFmt ("Chyba pri nacitani FlexibleSegmentu %s: %s",
			APIGuidToString (flexGuid).ToCStr ().Get (),
			flexRes.UnwrapErr ().text.c_str ());
		return false;
	}

	FlexibleSegment& flexSeg = *flexRes;

	// Zaznamenáme případně napojené RoutingElementy na koncích, abychom po otočení obnovili spojení
	std::vector<ACAPI::MEP::UniqueID> connectedRouteIds;
	for (const ACAPI::MEP::UniqueID& connId : flexSeg.GetDirectConnectedMEPElementIds ()) {
		const API_Guid connGuid = GSGuid2APIGuid (connId.GetGuid ());
		API_Elem_Head head = {};
		head.guid = connGuid;
		if (ACAPI_Element_GetHeader (&head) == NoError && head.type.typeID == API_ExternalElemID) {
			if (IsRoutingElement (head.type.classID)) {
				connectedRouteIds.push_back (connId);
			}
		}
	}

	// Varianta A: Spline geometrie (Vzduchotechnika i Potrubí)
	if (ACAPI::Result<std::vector<API_Coord3D>> ctrlPtsRes = flexSeg.GetControlPoints (); ctrlPtsRes.IsOk ()) {
		std::vector<API_Coord3D> reversedPoints = *ctrlPtsRes;
		if (reversedPoints.size () < 2) {
			Logger::DebugFmt ("FlexibleSegment %s ma mene nez 2 ridici body.", APIGuidToString (flexGuid).ToCStr ().Get ());
			return false;
		}

		std::reverse (reversedPoints.begin (), reversedPoints.end ());

		ACAPI::Result<void> modRes = flexSeg.Modify ([&] (FlexibleSegment::Modifier& modifier) -> GSErrCode {
			ACAPI::Result<void> setRes = modifier.SetControlPoints (reversedPoints);
			if (setRes.IsErr ()) {
				return setRes.UnwrapErr ().kind;
			}
			return NoError;
		}, "Otoceni smeru flexibilniho potrubi (Spline)");

		if (modRes.IsErr ()) {
			Logger::DebugFmt ("Chyba pri SetControlPoints u FlexibleSegmentu %s: %s",
				APIGuidToString (flexGuid).ToCStr ().Get (),
				modRes.UnwrapErr ().text.c_str ());
			return false;
		}
	}
	// Varianta B: Složená Polyline + Spline geometrie (Potrubí)
	else if (ACAPI::Result<FlexibleSegmentPolyline> polyRes = flexSeg.GetPolyline (); polyRes.IsOk ()) {
		FlexibleSegmentPolyline reversedPolyline = *polyRes;
		if (reversedPolyline.empty ()) {
			Logger::DebugFmt ("FlexibleSegment %s ma prazdnou polyline.", APIGuidToString (flexGuid).ToCStr ().Get ());
			return false;
		}

		std::reverse (reversedPolyline.begin (), reversedPolyline.end ());
		for (auto& part : reversedPolyline) {
			std::visit (GS::Overloaded {
				[] (FlexibleSegmentPolylineLinePart& line) {
					std::swap (line.begin, line.end);
				},
				[] (FlexibleSegmentPolylineSplinePart& spline) {
					std::reverse (spline.controlPoints.begin (), spline.controlPoints.end ());
				}
			}, part);
		}

		ACAPI::Result<void> modRes = flexSeg.Modify ([&] (FlexibleSegment::Modifier& modifier) -> GSErrCode {
			ACAPI::Result<void> setRes = modifier.SetPolyline (reversedPolyline);
			if (setRes.IsErr ()) {
				return setRes.UnwrapErr ().kind;
			}
			return NoError;
		}, "Otoceni smeru flexibilniho potrubi (Polyline)");

		if (modRes.IsErr ()) {
			Logger::DebugFmt ("Chyba pri SetPolyline u FlexibleSegmentu %s: %s",
				APIGuidToString (flexGuid).ToCStr ().Get (),
				modRes.UnwrapErr ().text.c_str ());
			return false;
		}
	} else {
		Logger::DebugFmt ("FlexibleSegment %s neobsahuje ani ControlPoints ani Polyline.", APIGuidToString (flexGuid).ToCStr ().Get ());
		return false;
	}

	// Obnovení logického napojení na případné sousední trasy
	const ACAPI::MEP::UniqueID flexUniqueId = Adapter::UniqueID (flexGuid);
	for (const ACAPI::MEP::UniqueID& routeId : connectedRouteIds) {
		if (ACAPI::Result<RoutingElement> routeRes = RoutingElement::Get (routeId); routeRes.IsOk ()) {
			routeRes->Modify ([&] (RoutingElement::Modifier& mod) -> GSErrCode {
				mod.ConnectLogically (flexUniqueId);
				return NoError;
			}, "Obnova napojeni flexibilniho potrubi");
		}
	}

	Logger::DebugFmt ("FlexibleSegment %s uspesne otocen.", APIGuidToString (flexGuid).ToCStr ().Get ());
	return true;
}

} // namespace OtocSmerTZB
