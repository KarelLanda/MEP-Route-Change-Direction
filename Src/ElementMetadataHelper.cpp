#include "ElementMetadataHelper.hpp"
#include "Logger.hpp"

namespace OtocSmerTZB {

bool ElementMetadataHelper::IsElementEditable (const API_Guid& elemGuid)
{
	if (elemGuid == APINULLGuid)
		return false;

	API_Elem_Head head = {};
	head.guid = elemGuid;
	if (ACAPI_Element_GetHeader (&head) != NoError)
		return false;

	if (head.lockId != 0) {
		Logger::DebugFmt ("Prvek %s je uzamcen (lockId != 0).", APIGuidToString (elemGuid).ToCStr ().Get ());
		return false;
	}

	// Kontrola uzamčené vrstvy
	if (head.layer != APIInvalidAttributeIndex) {
		API_Attribute layerAttr = {};
		layerAttr.header.typeID = API_LayerID;
		layerAttr.header.index  = head.layer;
		if (ACAPI_Attribute_Get (&layerAttr) == NoError) {
			if ((layerAttr.layer.head.flags & APILay_Locked) != 0) {
				Logger::DebugFmt ("Prvek %s lezi na uzamcene vrstve.", APIGuidToString (elemGuid).ToCStr ().Get ());
				return false;
			}
		}
	}

	// Kontrola editovatelnosti (vrstva, Teamwork pracovní prostor a oprávnění)
	if (!ACAPI_Element_Filter (elemGuid, APIFilt_IsEditable | APIFilt_InMyWorkspace | APIFilt_HasAccessRight)) {
		Logger::DebugFmt ("Prvek %s neprosel filtrem editovatelnosti (APIFilt_IsEditable / Teamwork).", APIGuidToString (elemGuid).ToCStr ().Get ());
		return false;
	}

	return true;
}


ElementMetadataSnapshot ElementMetadataHelper::Capture (const API_Guid& elemGuid)
{
	ElementMetadataSnapshot snap = {};
	snap.originalGuid = elemGuid;

	API_Element elem = {};
	elem.header.guid = elemGuid;
	if (ACAPI_Element_Get (&elem) == NoError) {
		snap.floorInd             = elem.header.floorInd;
		snap.layer                = elem.header.layer;
		snap.renovationStatus     = elem.header.renovationStatus;
		snap.renovationFilterGuid = elem.header.renovationFilterGuid;
		snap.groupGuid            = elem.header.groupGuid;
	}

	// Klasifikace
	if (ACAPI_Element_GetClassificationItems (elemGuid, snap.classifications) == NoError) {
		Logger::DebugFmt ("Zaznamenan pocet klasifikaci pro %s: %u",
			APIGuidToString (elemGuid).ToCStr ().Get (),
			static_cast<unsigned> (snap.classifications.GetSize ()));
	}

	// Vlastnosti (vestavěné vč. ID prvku, Nosné funkce, Umístění i uživatelské/IFC)
	GS::Array<API_PropertyDefinition> definitions;
	if (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_All, definitions) == NoError) {
		GS::Array<API_PropertyDefinition> editableDefs;
		for (const API_PropertyDefinition& def : definitions) {
			if (ACAPI_Element_IsPropertyDefinitionValueEditable (elemGuid, def.guid)) {
				editableDefs.Push (def);
			}
		}

		if (!editableDefs.IsEmpty ()) {
			GS::Array<API_Property> allProps;
			if (ACAPI_Element_GetPropertyValues (elemGuid, editableDefs, allProps) == NoError) {
				for (const API_Property& prop : allProps) {
					if (prop.status == API_Property_HasValue && !prop.isDefault) {
						snap.customProperties.Push (prop);
					}
				}
			}
		}
	}

	// Asociativní popisky (API_LabelID) připojené přímo k prvku
	GS::Array<API_Guid> connectedLabels;
	if (ACAPI_Grouping_GetConnectedElements (elemGuid, API_LabelID, &connectedLabels) == NoError) {
		for (const API_Guid& lblGuid : connectedLabels) {
			LabelSnapshot lblSnap = {};
			lblSnap.labelGuid = lblGuid;
			lblSnap.labelElement.header.guid = lblGuid;
			if (ACAPI_Element_Get (&lblSnap.labelElement) == NoError) {
				snap.attachedLabels.push_back (lblSnap);
			}
		}
		if (!snap.attachedLabels.empty ()) {
			Logger::DebugFmt ("Zaznamenano %zu asociativnich popisku na prvku %s.",
				snap.attachedLabels.size (),
				APIGuidToString (elemGuid).ToCStr ().Get ());
		}
	}

	return snap;
}


void ElementMetadataHelper::Restore (const API_Guid& targetElemGuid, const ElementMetadataSnapshot& snapshot)
{
	if (targetElemGuid == APINULLGuid)
		return;

	// 1. Obnova hlavičky (vrstva, domovské podlaží, stav rekonstrukce)
	API_Element elem = {};
	elem.header.guid = targetElemGuid;
	if (ACAPI_Element_Get (&elem) == NoError) {
		API_Element mask = {};
		ACAPI_ELEMENT_MASK_CLEAR (mask);

		bool needChange = false;
		if (elem.header.floorInd != snapshot.floorInd) {
			elem.header.floorInd = snapshot.floorInd;
			ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, floorInd);
			needChange = true;
		}
		if ( snapshot.layer != APIInvalidAttributeIndex && elem.header.layer != snapshot.layer) {
			elem.header.layer = snapshot.layer;
			ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
			needChange = true;
		}
		if (elem.header.renovationStatus != snapshot.renovationStatus) {
			elem.header.renovationStatus = snapshot.renovationStatus;
			ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, renovationStatus);
			needChange = true;
		}
		if (elem.header.renovationFilterGuid != snapshot.renovationFilterGuid) {
			elem.header.renovationFilterGuid = snapshot.renovationFilterGuid;
			ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, renovationFilterGuid);
			needChange = true;
		}

		if (needChange) {
			GSErrCode chgErr = ACAPI_Element_Change (&elem, &mask, nullptr, 0, true);
			if (chgErr != NoError) {
				Logger::DebugFmt ("Upozorneni: ACAPI_Element_Change pro hlavicku vratilo chybu %d.", static_cast<int> (chgErr));
			}
		}
	}

	// 2. Obnova klasifikací
	for (const auto& pair : snapshot.classifications) {
		const API_Guid& itemGuid = pair.second;
		if (itemGuid != APINULLGuid) {
			ACAPI_Element_AddClassificationItem (targetElemGuid, itemGuid);
		}
	}

	// 3. Obnova vlastností (vč. ID prvku a uživatelských vlastností)
	if (!snapshot.customProperties.IsEmpty ()) {
		for (const API_Property& prop : snapshot.customProperties) {
			ACAPI_Element_SetProperty (targetElemGuid, prop);
		}
	}

	// 4. Obnova asociativních popisků (pokud po smazání původního prvku ztratily vazbu)
	for (const LabelSnapshot& lblSnap : snapshot.attachedLabels) {
		API_Element currentLbl = {};
		currentLbl.header.guid = lblSnap.labelGuid;
		if (ACAPI_Element_Get (&currentLbl) == NoError) {
			if (currentLbl.label.parent != targetElemGuid) {
				API_Element lblMask = {};
				ACAPI_ELEMENT_MASK_CLEAR (lblMask);
				ACAPI_ELEMENT_MASK_SET (lblMask, API_LabelType, parent);
				ACAPI_ELEMENT_MASK_SET (lblMask, API_LabelType, parentType);
				currentLbl.label.parent     = targetElemGuid;
				currentLbl.label.parentType = API_ExternalElemID;
				ACAPI_Element_Change (&currentLbl, &lblMask, nullptr, 0, true);
			}
		}
	}
}

} // namespace OtocSmerTZB
