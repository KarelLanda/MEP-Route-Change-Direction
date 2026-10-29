#ifndef ELEMENT_METADATA_HELPER_HPP
#define ELEMENT_METADATA_HELPER_HPP

#include "ACAPinc.h"
#include "API_Guid.hpp"
#include <vector>

namespace OtocSmerTZB {

struct LabelSnapshot {
	API_Guid    labelGuid = APINULLGuid;
	API_Element labelElement = {};
};

struct ElementMetadataSnapshot {
	API_Guid                                   originalGuid = APINULLGuid;
	short                                      floorInd = 0;
	API_AttributeIndex                         layer = APIInvalidAttributeIndex;
	API_RenovationStatusType                   renovationStatus = API_UndefinedStatus;
	API_Guid                                   renovationFilterGuid = APINULLGuid;
	API_Guid                                   groupGuid = APINULLGuid;
	GS::Array<GS::Pair<API_Guid, API_Guid>>    classifications;
	GS::Array<API_Property>                    customProperties;
	std::vector<LabelSnapshot>                 attachedLabels;
};

class ElementMetadataHelper {
public:
	// Ověří, zda je prvek editovatelný (není uzamčen, není na uzamčené vrstvě, je rezervován v Teamworku)
	static bool IsElementEditable (const API_Guid& elemGuid);

	// Zálohuje hlavičku, klasifikace, upravené vlastnosti a připojené asociativní popisky
	static ElementMetadataSnapshot Capture (const API_Guid& elemGuid);

	// Obnoví hlavičku, klasifikace, upravené vlastnosti a připojené asociativní popisky na nově vytvořený prvek
	static void Restore (const API_Guid& targetElemGuid, const ElementMetadataSnapshot& snapshot);
};

} // namespace OtocSmerTZB

#endif // ELEMENT_METADATA_HELPER_HPP
