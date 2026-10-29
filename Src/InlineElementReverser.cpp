#include "InlineElementReverser.hpp"
#include "RoutingElementReverser.hpp"
#include "Logger.hpp"

// MEPAPI
#include "ACAPI/MEPAdapter.hpp"
#include "ACAPI/MEPModifiableElement.hpp"
#include "ACAPI/MEPPort.hpp"
#include "ACAPI/MEPRoutingElement.hpp"

#include <cmath>
#include <set>
#include <vector>

using namespace ACAPI::MEP;

namespace OtocSmerTZB {

InlineReverseStatus InlineElementReverser::Reverse (const API_Guid& elemGuid)
{
	Logger::DebugFmt ("Zahajuji otaceni vlozeneho MEP prvku (Accessory/Fitting) %s...",
		APIGuidToString (elemGuid).ToCStr ().Get ());

	const Adapter::UniqueID uniqueId (elemGuid);
	ACAPI::Result<ModifiableElement> modElemRes = ModifiableElement::Get (uniqueId);
	if (modElemRes.IsErr ()) {
		Logger::DebugFmt ("Nelze nacist ModifiableElement %s: %s",
			APIGuidToString (elemGuid).ToCStr ().Get (),
			modElemRes.UnwrapErr ().text.c_str ());
		return InlineReverseStatus::Error;
	}

	ModifiableElement& elem = *modElemRes;
	const std::vector<ACAPI::MEP::UniqueID> portIds = elem.GetPortIDs ();
	if (portIds.size () != 2) {
		Logger::DebugFmt ("Prvek %s ma %zu portu (podporovany jsou pouze 2-portove vlozene prvky).",
			APIGuidToString (elemGuid).ToCStr ().Get (),
			portIds.size ());
		return InlineReverseStatus::NotTwoPortElement;
	}

	ACAPI::Result<Port> port0Res = Port::Get (portIds[0]);
	ACAPI::Result<Port> port1Res = Port::Get (portIds[1]);
	if (port0Res.IsErr () || port1Res.IsErr ()) {
		return InlineReverseStatus::Error;
	}

	const API_Coord3D pos0 = port0Res->GetPosition ();
	const API_Coord3D pos1 = port1Res->GetPosition ();

	// Střed mezi oběma porty v 3D prostoru musí zůstat po otočení na identickém místě
	const API_Coord3D midPoint = {
		0.5 * (pos0.x + pos1.x),
		0.5 * (pos0.y + pos1.y),
		0.5 * (pos0.z + pos1.z)
	};

	// Zjistíme sousední trasy (RoutingElement), které jsou k prvku připojené, abychom po otočení obnovili logické napojení
	std::set<API_Guid> connectedRouteGuids;
	for (const ACAPI::MEP::UniqueID& pId : portIds) {
		if (ACAPI::Result<Port> pRes = Port::Get (pId); pRes.IsOk ()) {
			if (std::optional<ACAPI::MEP::UniqueID> connElemId = pRes->GetConnectedMEPElementId (); connElemId.has_value ()) {
				const API_Guid connGuid = GSGuid2APIGuid (connElemId->GetGuid ());
				API_Elem_Head head = {};
				head.guid = connGuid;
				if (ACAPI_Element_GetHeader (&head) == NoError && head.type.typeID == API_ExternalElemID) {
					const API_Guid parentRoute = RoutingElementReverser::ResolveParentRoutingElement (connGuid, head.type.classID);
					if (parentRoute != APINULLGuid) {
						connectedRouteGuids.insert (parentRoute);
					}
				}
			}
		}
	}

	const API_Coord3D oldAnchor = elem.GetAnchorPoint ();
	const Orientation oldOrient = elem.GetOrientation ();

	// Normalizace směrového a rotačního vektoru
	API_Vector3D d = oldOrient.direction;
	const double dLen = std::sqrt (d.x * d.x + d.y * d.y + d.z * d.z);
	if (dLen > 1e-9) {
		d.x /= dLen; d.y /= dLen; d.z /= dLen;
	}

	API_Vector3D r = oldOrient.rotation;
	const double rLen = std::sqrt (r.x * r.y + r.y * r.y + r.z * r.z);
	if (rLen > 1e-9) {
		const double normR = std::sqrt (r.x * r.x + r.y * r.y + r.z * r.z);
		r.x /= normR; r.y /= normR; r.z /= normR;
	}

	// Boční vektor s = d x r
	const API_Vector3D s = {
		d.y * r.z - d.z * r.y,
		d.z * r.x - d.x * r.z,
		d.x * r.y - d.y * r.x
	};

	// Vektor od středu mezi porty M ke kotevnímu bodu
	const API_Vector3D delta = {
		oldAnchor.x - midPoint.x,
		oldAnchor.y - midPoint.y,
		oldAnchor.z - midPoint.z
	};

	const double deltaD = delta.x * d.x + delta.y * d.y + delta.z * d.z;
	const double deltaR = delta.x * r.x + delta.y * r.y + delta.z * r.z;
	const double deltaS = delta.x * s.x + delta.y * s.y + delta.z * s.z;

	// Při otočení d -> -d a zachování r -> +r se boční osa s -> -s rovněž otočí.
	// Nový kotevní bod tak zachová přesnou 3D polohu portů P0 <-> P1.
	const API_Coord3D newAnchor = {
		midPoint.x - deltaD * d.x + deltaR * r.x - deltaS * s.x,
		midPoint.y - deltaD * d.y + deltaR * r.y - deltaS * s.y,
		midPoint.z - deltaD * d.z + deltaR * r.z - deltaS * s.z
	};

	const Orientation newOrient = {
		{ -oldOrient.direction.x, -oldOrient.direction.y, -oldOrient.direction.z },
		oldOrient.rotation
	};

	ACAPI::Result<void> modRes = elem.Modify ([&] (ModifiableElement::Modifier& modifier) {
		modifier.SetAnchorPoint (newAnchor);
		modifier.SetOrientation (newOrient);
	}, "Otoceni smeru vlozeneho TZB prvku");

	if (modRes.IsErr ()) {
		Logger::DebugFmt ("Chyba pri modifikaci prvku %s: %s",
			APIGuidToString (elemGuid).ToCStr ().Get (),
			modRes.UnwrapErr ().text.c_str ());
		return InlineReverseStatus::Error;
	}

	// Obnovení logického napojení na sousední trasy
	for (const API_Guid& routeGuid : connectedRouteGuids) {
		if (ACAPI::Result<RoutingElement> routeRes = RoutingElement::Get (Adapter::UniqueID (routeGuid)); routeRes.IsOk ()) {
			routeRes->Modify ([&] (RoutingElement::Modifier& mod) -> GSErrCode {
				mod.ConnectLogically (uniqueId);
				return NoError;
			}, "Obnova napojeni trasy k otocenemu prvku");
		}
	}

	Logger::DebugFmt ("Vlozeny 2-portovy prvek %s uspesne otocen.", APIGuidToString (elemGuid).ToCStr ().Get ());
	return InlineReverseStatus::Success;
}

} // namespace OtocSmerTZB
