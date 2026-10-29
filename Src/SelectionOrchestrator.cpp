#include "SelectionOrchestrator.hpp"
#include "RoutingElementReverser.hpp"
#include "FlexibleSegmentReverser.hpp"
#include "InlineElementReverser.hpp"
#include "ElementMetadataHelper.hpp"
#include "Messages.hpp"
#include "Logger.hpp"

// MEPAPI
#include "ACAPI/MEPAdapter.hpp"

#include <set>
#include <vector>

using namespace ACAPI::MEP;

namespace OtocSmerTZB {

GSErrCode SelectionOrchestrator::ExecuteOnCurrentSelection ()
{
	Logger::Debug ("Spusten prikaz 'Otocit smer TZB trasy'.");

	API_SelectionInfo selectionInfo = {};
	GS::Array<API_Neig> selNeigs;
	GSErrCode selErr = ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false);
	BMKillHandle (reinterpret_cast<GSHandle*> (&selectionInfo.marquee.coords));

	if (selErr == APIERR_NOSEL || selectionInfo.typeID == API_SelEmpty || selNeigs.IsEmpty ()) {
		Logger::ReportAlert (Messages::kNoSelection);
		return NoError;
	}

	std::vector<API_Guid> routeList;
	std::set<API_Guid>    seenRoutes;

	std::vector<API_Guid> flexList;
	std::set<API_Guid>    seenFlex;

	std::vector<API_Guid> inlineList;
	std::set<API_Guid>    seenInline;

	for (const API_Neig& neig : selNeigs) {
		if (neig.guid == APINULLGuid)
			continue;

		API_Element elem = {};
		elem.header.guid = neig.guid;
		if (ACAPI_Element_Get (&elem) != NoError)
			continue;

		// Podpora výběru v řezech / pohledech / vnitřních pohledech (API_SectElemID)
		if (elem.header.type.typeID == API_SectElemID) {
			const API_Guid parentGuid = elem.sectElem.parentGuid;
			if (parentGuid == APINULLGuid)
				continue;
			elem = {};
			elem.header.guid = parentGuid;
			if (ACAPI_Element_Get (&elem) != NoError)
				continue;
		}

		if (elem.header.type.typeID != API_ExternalElemID)
			continue;

		const API_Guid elemGuid = elem.header.guid;
		const API_Guid classId  = elem.header.type.classID;

		// 1. Segmentovaná trasa (nebo její vybraný podprvek: RigidSegment, Elbow, Transition, RoutingSegment, RoutingNode)
		const API_Guid parentRouteGuid = RoutingElementReverser::ResolveParentRoutingElement (elemGuid, classId);
		if (parentRouteGuid != APINULLGuid) {
			if (seenRoutes.insert (parentRouteGuid).second) {
				routeList.push_back (parentRouteGuid);
			}
			continue;
		}

		// 2. Flexibilní potrubí / hadice (FlexibleSegment)
		if (IsFlexibleSegment (classId)) {
			if (seenFlex.insert (elemGuid).second) {
				flexList.push_back (elemGuid);
			}
			continue;
		}

		// 3. Vložená armatura (Accessory) nebo tvarovka (Fitting)
		if (IsAccessory (classId) || IsFitting (classId)) {
			if (seenInline.insert (elemGuid).second) {
				inlineList.push_back (elemGuid);
			}
			continue;
		}
	}

	if (routeList.empty () && flexList.empty () && inlineList.empty ()) {
		Logger::ReportAlert (Messages::kNoSupportedMepElements);
		return NoError;
	}

	// Kontrola uzamčení a editovatelnosti
	std::vector<API_Guid> editableRoutes;
	std::vector<API_Guid> editableFlex;
	std::vector<API_Guid> editableInline;
	size_t lockedCount = 0;

	for (const API_Guid& g : routeList) {
		if (ElementMetadataHelper::IsElementEditable (g))
			editableRoutes.push_back (g);
		else
			++lockedCount;
	}
	for (const API_Guid& g : flexList) {
		if (ElementMetadataHelper::IsElementEditable (g))
			editableFlex.push_back (g);
		else
			++lockedCount;
	}
	for (const API_Guid& g : inlineList) {
		if (ElementMetadataHelper::IsElementEditable (g))
			editableInline.push_back (g);
		else
			++lockedCount;
	}

	if (editableRoutes.empty () && editableFlex.empty () && editableInline.empty ()) {
		Logger::ReportAlert (Messages::kElementLockedOrNotEditable);
		return NoError;
	}

	size_t successCount      = 0;
	size_t notTwoPortCount   = 0;
	size_t failedCount       = 0;
	GS::Array<API_Neig> neigsToReselect;

	const GS::UniString undoName (Messages::kUndoCommandName, CC_UTF8);
	GSErrCode cmdErr = ACAPI_CallUndoableCommand (undoName, [&] () -> GSErrCode {
		// 1. Otočení segmentovaných tras
		for (const API_Guid& routeGuid : editableRoutes) {
			const API_Guid newGuid = RoutingElementReverser::Reverse (routeGuid);
			if (newGuid != APINULLGuid) {
				++successCount;
				API_Neig n = {};
				n.guid = newGuid;
				neigsToReselect.Push (n);
			} else {
				++failedCount;
			}
		}

		// 2. Otočení flexibilního potrubí
		for (const API_Guid& flexGuid : editableFlex) {
			if (FlexibleSegmentReverser::Reverse (flexGuid)) {
				++successCount;
				API_Neig n = {};
				n.guid = flexGuid;
				neigsToReselect.Push (n);
			} else {
				++failedCount;
			}
		}

		// 3. Otočení vložených 2-portových armatur a tvarovek
		for (const API_Guid& inlineGuid : editableInline) {
			const InlineReverseStatus status = InlineElementReverser::Reverse (inlineGuid);
			if (status == InlineReverseStatus::Success) {
				++successCount;
				API_Neig n = {};
				n.guid = inlineGuid;
				neigsToReselect.Push (n);
			} else if (status == InlineReverseStatus::NotTwoPortElement) {
				++notTwoPortCount;
			} else {
				++failedCount;
			}
		}

		return (successCount > 0) ? NoError : APIERR_CANCEL;
	});

	// Obnovení výběru na otočených trasách / prvcích
	if (cmdErr == NoError && !neigsToReselect.IsEmpty ()) {
		ACAPI_Selection_Select (neigsToReselect, true);
	}

	Logger::DebugFmt ("Dokonceno: uspesne=%zu, uzamcene=%zu, ne-2portove=%zu, chyby=%zu.",
		successCount, lockedCount, notTwoPortCount, failedCount);

	// Případná upozornění na částečné či úplné hraniční stavy
	if (lockedCount > 0) {
		Logger::ReportAlert (Messages::kElementLockedOrNotEditable);
	}
	if (notTwoPortCount > 0 && successCount == 0) {
		Logger::ReportAlert (Messages::kInlineElementNotTwoPort);
	}
	if (failedCount > 0) {
		Logger::ReportAlert (Messages::kPartialOrFullFailure);
	}

	return NoError;
}

} // namespace OtocSmerTZB
