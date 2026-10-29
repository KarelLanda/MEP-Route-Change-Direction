#include "APIEnvir.h"
#include "ACAPinc.h"

#include "ResourceIDs.hpp"
#include "SelectionOrchestrator.hpp"
#include "Logger.hpp"

// =============================================================================
// Menu Command Handler
// =============================================================================

static GSErrCode MenuCommandHandler (const API_MenuParams* menuParams)
{
	if (menuParams == nullptr)
		return NoError;

	switch (menuParams->menuItemRef.menuResID) {
		case ID_ADDON_MENU:
			switch (menuParams->menuItemRef.itemIndex) {
				case MENU_ITEM_REVERSE_MEP:
					return OtocSmerTZB::SelectionOrchestrator::ExecuteOnCurrentSelection ();
				default:
					break;
			}
			break;
		default:
			break;
	}

	return NoError;
}

// =============================================================================
// Required Add-On Lifecycle Callbacks
// =============================================================================

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, ID_ADDON_INFO, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, ID_ADDON_INFO, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


GSErrCode RegisterInterface (void)
{
	return ACAPI_MenuItem_RegisterMenu (ID_ADDON_MENU, ID_ADDON_PROMPTS, MenuCode_UserDef, MenuFlag_Default);
}


GSErrCode Initialize (void)
{
	OtocSmerTZB::Logger::Debug ("Doplnek OtocSmerTZB inicializovan.");
	return ACAPI_MenuItem_InstallMenuHandler (ID_ADDON_MENU, MenuCommandHandler);
}


GSErrCode FreeData (void)
{
	return NoError;
}
