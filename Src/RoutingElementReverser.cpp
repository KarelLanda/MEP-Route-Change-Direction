#include "RoutingElementReverser.hpp"
#include "ElementMetadataHelper.hpp"
#include "Logger.hpp"

// MEPAPI
#include "ACAPI/MEPAdapter.hpp"
#include "ACAPI/MEPElement.hpp"
#include "ACAPI/MEPPort.hpp"
#include "ACAPI/MEPRoutingElement.hpp"
#include "ACAPI/MEPRoutingElementDefault.hpp"
#include "ACAPI/MEPRoutingSegment.hpp"
#include "ACAPI/MEPRoutingSegmentDefault.hpp"
#include "ACAPI/MEPRigidSegment.hpp"
#include "ACAPI/MEPRigidSegmentDefault.hpp"
#include "ACAPI/MEPRoutingNode.hpp"
#include "ACAPI/MEPRoutingNodeDefault.hpp"
#include "ACAPI/MEPElbow.hpp"
#include "ACAPI/MEPElbowDefault.hpp"
#include "ACAPI/MEPTransition.hpp"
#include "ACAPI/MEPTransitionDefault.hpp"
#include "ACAPI/MEPTakeOff.hpp"
#include "ACAPI/MEPTakeOffDefault.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#if defined (macintosh)
#include <dlfcn.h>
#elif defined (WINDOWS)
#include "Win32Interface.hpp"
#endif
#include <map>
#include <set>
#include <vector>

using namespace ACAPI::MEP;

struct OtocSmerTZBTag {};

namespace ACAPI::Impl {
template <>
class ImplAccessor<OtocSmerTZBTag> {
public:
	template <typename T>
	static const void* GetODBRefPtr (const T& apiObj)
	{
		const void* implPtr = apiObj.mImpl.get ();
		if (implPtr == nullptr)
			return nullptr;
		// V ACAPI::Impl::DatabaseObjectImpl je ODB::Ref<MEPElem::...> uložen na offsetu +0x18
		return reinterpret_cast<const char*> (implPtr) + 0x18;
	}
};
} // namespace ACAPI::Impl

namespace OtocSmerTZB {

namespace {

using NativeODBRefAccessor = ACAPI::Impl::ImplAccessor<OtocSmerTZBTag>;

#if defined (WINDOWS)
FARPROC FindExportedSymbolByPrefix (HMODULE hMod, const char* prefix, const char* excludeSubstr = nullptr)
{
	if (hMod == nullptr || prefix == nullptr)
		return nullptr;

	if (FARPROC exact = GetProcAddress (hMod, prefix); exact != nullptr)
		return exact;

	const auto* base = reinterpret_cast<const std::uint8_t*> (hMod);
	const auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*> (base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return nullptr;

	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*> (base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return nullptr;

	const auto& expDirEntry = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (expDirEntry.VirtualAddress == 0 || expDirEntry.Size == 0)
		return nullptr;

	const auto* expDir   = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*> (base + expDirEntry.VirtualAddress);
	const auto* names    = reinterpret_cast<const DWORD*> (base + expDir->AddressOfNames);
	const auto* ordinals = reinterpret_cast<const WORD*>  (base + expDir->AddressOfNameOrdinals);

	const std::size_t prefixLen = std::strlen (prefix);
	for (DWORD i = 0; i < expDir->NumberOfNames; ++i) {
		const char* name = reinterpret_cast<const char*> (base + names[i]);
		if (std::strncmp (name, prefix, prefixLen) == 0) {
			if (excludeSubstr != nullptr && std::strstr (name, excludeSubstr) != nullptr)
				continue;
			const WORD ordinalIndex = ordinals[i];
			if (ordinalIndex < expDir->NumberOfFunctions)
				return GetProcAddress (hMod, name);
		}
	}
	return nullptr;
}
#endif

struct NativeMEPSymbols {
	using ProcessModificationRequestFn = void* (*) (const void* odbRefUntyped);
	using ReverseDirectionFn           = void  (*) (void* mepRoutingElement);
	using UpdateConnectionAtNodeFn     = void  (*) (const void* nodeRef, const void* idSpace);
	using UpdateRoutingElementFn       = void  (*) (const void* routeRef);
	using SetAnchorFn                  = void  (*) (void* mepRoutingSegment, int anchor);
	using SetReferenceOffsetXFn        = void  (*) (void* mepRoutingSegment, double offsetX);
	using SetReferenceOffsetYFn        = void  (*) (void* mepRoutingSegment, double offsetY);
	using SetPreferredTransPlacementFn = void  (*) (void* mepRoutingNode, int placement);

	ProcessModificationRequestFn processModificationRequest = nullptr;
	ReverseDirectionFn           reverseDirection           = nullptr;
	UpdateConnectionAtNodeFn     updateConnectionAtNode     = nullptr;
	UpdateRoutingElementFn       updateRoutingElement       = nullptr;
	SetAnchorFn                  setSegmentAnchor           = nullptr;
	SetReferenceOffsetXFn        setSegmentOffsetX          = nullptr;
	SetReferenceOffsetYFn        setSegmentOffsetY          = nullptr;
	SetPreferredTransPlacementFn setNodeTransitionPlacement = nullptr;

	bool IsAvailable () const
	{
		return processModificationRequest != nullptr
			&& reverseDirection != nullptr
			&& updateRoutingElement != nullptr
			&& setSegmentAnchor != nullptr
			&& setSegmentOffsetX != nullptr
			&& setSegmentOffsetY != nullptr
			&& setNodeTransitionPlacement != nullptr;
	}

	static const NativeMEPSymbols& Get ()
	{
		static const NativeMEPSymbols instance = [] () {
			NativeMEPSymbols s;
#if defined (macintosh)
			s.processModificationRequest = reinterpret_cast<ProcessModificationRequestFn> (
				dlsym (RTLD_DEFAULT, "_ZNK3ODB10RefUntyped26ProcessModificationRequestEv"));
			s.reverseDirection = reinterpret_cast<ReverseDirectionFn> (
				dlsym (RTLD_DEFAULT, "_ZN7MEPElem14RoutingElement16ReverseDirectionEv"));
			s.updateConnectionAtNode = reinterpret_cast<UpdateConnectionAtNodeFn> (
				dlsym (RTLD_DEFAULT, "_ZN7MEPElem15RouteGeneration22UpdateConnectionAtNodeERKN3ODB3RefINS_11RoutingNodeEEERKNS1_7IdSpaceE"));
			s.updateRoutingElement = reinterpret_cast<UpdateRoutingElementFn> (
				dlsym (RTLD_DEFAULT, "_ZN7MEPElem15RouteGeneration20UpdateRoutingElementERKN3ODB3RefINS_14RoutingElementEEE"));
			s.setSegmentAnchor = reinterpret_cast<SetAnchorFn> (
				dlsym (RTLD_DEFAULT, "_ZN7MEPElem14RoutingSegment9SetAnchorEN6VBElem6AnchorE"));
			s.setSegmentOffsetX = reinterpret_cast<SetReferenceOffsetXFn> (
				dlsym (RTLD_DEFAULT, "_ZN7MEPElem14RoutingSegment19SetReferenceOffsetXEd"));
			s.setSegmentOffsetY = reinterpret_cast<SetReferenceOffsetYFn> (
				dlsym (RTLD_DEFAULT, "_ZN7MEPElem14RoutingSegment19SetReferenceOffsetYEd"));
			s.setNodeTransitionPlacement = reinterpret_cast<SetPreferredTransPlacementFn> (
				dlsym (RTLD_DEFAULT, "_ZN7MEPElem11RoutingNode31SetPreferredTransitionPlacementENS_15RouteGeneration28PreferredTransitionPlacementE"));
#elif defined (WINDOWS)
			HMODULE hMepElements = GetModuleHandleW (L"MEPElements.dll");
			HMODULE hODB         = GetModuleHandleW (L"ObjectDatabase.dll");
			if (hODB != nullptr) {
				s.processModificationRequest = reinterpret_cast<ProcessModificationRequestFn> (
					FindExportedSymbolByPrefix (hODB, "?ProcessModificationRequest@RefUntyped@ODB@@", "ModificationAuthorizationControl"));
			}
			if (hMepElements != nullptr) {
				s.reverseDirection = reinterpret_cast<ReverseDirectionFn> (
					FindExportedSymbolByPrefix (hMepElements, "?ReverseDirection@RoutingElement@MEPElem@@"));
				s.updateConnectionAtNode = reinterpret_cast<UpdateConnectionAtNodeFn> (
					FindExportedSymbolByPrefix (hMepElements, "?UpdateConnectionAtNode@RouteGeneration@MEPElem@@"));
				s.updateRoutingElement = reinterpret_cast<UpdateRoutingElementFn> (
					FindExportedSymbolByPrefix (hMepElements, "?UpdateRoutingElement@RouteGeneration@MEPElem@@"));
				s.setSegmentAnchor = reinterpret_cast<SetAnchorFn> (
					FindExportedSymbolByPrefix (hMepElements, "?SetAnchor@RoutingSegment@MEPElem@@"));
				s.setSegmentOffsetX = reinterpret_cast<SetReferenceOffsetXFn> (
					FindExportedSymbolByPrefix (hMepElements, "?SetReferenceOffsetX@RoutingSegment@MEPElem@@"));
				s.setSegmentOffsetY = reinterpret_cast<SetReferenceOffsetYFn> (
					FindExportedSymbolByPrefix (hMepElements, "?SetReferenceOffsetY@RoutingSegment@MEPElem@@"));
				s.setNodeTransitionPlacement = reinterpret_cast<SetPreferredTransPlacementFn> (
					FindExportedSymbolByPrefix (hMepElements, "?SetPreferredTransitionPlacement@RoutingNode@MEPElem@@"));
			}
#endif
			return s;
		} ();
		return instance;
	}
};

const void* GetODBRefIdSpace (const void* odbRefPtr)
{
	if (odbRefPtr == nullptr)
		return nullptr;
	const void* objectInfo = *reinterpret_cast<void* const*> (odbRefPtr);
	if (objectInfo == nullptr)
		return nullptr;
	const uint64_t flags = *reinterpret_cast<const uint64_t*> (reinterpret_cast<const char*> (objectInfo) + 0x40);
	if ((flags & 0x3ULL) == 0)
		return nullptr;
	return *reinterpret_cast<void* const*> (reinterpret_cast<const char*> (objectInfo) + 0x08);
}

constexpr double kPi = 3.14159265358979323846;

struct TakeOffSnapshot {
	API_Guid                originalGuid = APINULLGuid;
	TakeOffDefault          takeOffDefault;
	double                  length = 0.0;
	double                  distanceFromOldBegin = 0.0;
	double                  newRotationAroundAxis = 0.0;
	ElementMetadataSnapshot metadata;
};

struct SegmentSnapshot {
	RoutingSegmentDefault          segDefault;
	RigidSegmentDefault            rigidDefault;
	ConnectorShape                 shape = ConnectorShape::Rectangular;
	double                         width = 0.0;
	double                         height = 0.0;
	std::optional<uint32_t>        referenceId;
	ACAPI::MEP::UniqueID           preferenceTableId;
	API_AnchorID                   anchor = APIAnc_MM;
	double                         offsetX = 0.0;
	double                         offsetY = 0.0;
	double                         wallThickness = 0.0;
	ConnectionStyle                connectionStyle = ConnectionStyle::SimpleBody;
	std::optional<double>          flangeSize;
	std::vector<TakeOffSnapshot>   takeOffs;
};

struct NodeSnapshot {
	RoutingNodeDefault           nodeDefault;
	ElbowDefault                 elbowDefault;
	TransitionDefault            transitionDefault;
	PreferredTransitionPlacement preferredTransitionPlacement = PreferredTransitionPlacement::SmallToLarge;
	bool                         hasTransition = false;
	double                       transitionLength = 0.0;
};

double Distance3D (const API_Coord3D& a, const API_Coord3D& b)
{
	const double dx = b.x - a.x;
	const double dy = b.y - a.y;
	const double dz = b.z - a.z;
	return std::sqrt (dx * dx + dy * dy + dz * dz);
}

bool IsPurelyVerticalSegment (const API_Coord3D& a, const API_Coord3D& b)
{
	const double dx = b.x - a.x;
	const double dy = b.y - a.y;
	const double dz = b.z - a.z;
	const double horizLen = std::sqrt (dx * dx + dy * dy);
	return (horizLen < 1e-5 && std::abs (dz) > 1e-5);
}

// Vypočítá úhel rotationAroundMainAxis pro odbočku (TakeOff) na otočeném segmentu (newBegin -> newEnd)
// z jejího 3D směrového vektoru.
// Konvence Archicad MEP (viz PlaceElements.cpp):
//   0.0      = vlevo (Left) při pohledu po směru segmentu
//   PI * 0.5 = nahoru (Up)
//   PI       = vpravo (Right)
//   PI * 1.5 = dolů (Down)
double ComputeTakeOffRotationOnReversedSegment (
	const API_Coord3D& newSegBegin,
	const API_Coord3D& newSegEnd,
	const API_Vector3D& takeOffDir,
	double fallbackBaseRotation)
{
	const double ux = newSegEnd.x - newSegBegin.x;
	const double uy = newSegEnd.y - newSegBegin.y;
	const double uz = newSegEnd.z - newSegBegin.z;
	const double uLen = std::sqrt (ux * ux + uy * uy + uz * uz);
	if (uLen < 1e-6) {
		double angle = kPi - fallbackBaseRotation;
		angle = std::fmod (angle, 2.0 * kPi);
		if (angle < 0.0)
			angle += 2.0 * kPi;
		return angle;
	}

	const double dx = ux / uLen;
	const double dy = uy / uLen;
	const double dz = uz / uLen;

	const double horizLen = std::sqrt (dx * dx + dy * dy);
	if (horizLen > 1e-6) {
		// Jednotkový vektor "vlevo" v horizontální rovině: (-dy, dx, 0) / horizLen
		const double leftX = -dy / horizLen;
		const double leftY =  dx / horizLen;
		const double leftZ =  0.0;

		// Jednotkový vektor "nahoru" kolmý na směr segmentu a vektor vlevo: u x left
		const double upX = dy * leftZ - dz * leftY;
		const double upY = dz * leftX - dx * leftZ;
		const double upZ = dx * leftY - dy * leftX;

		const double projLeft = takeOffDir.x * leftX + takeOffDir.y * leftY + takeOffDir.z * leftZ;
		const double projUp   = takeOffDir.x * upX   + takeOffDir.y * upY   + takeOffDir.z * upZ;

		if (projLeft * projLeft + projUp * projUp > 1e-6) {
			double angle = std::atan2 (projUp, projLeft);
			if (angle < 0.0)
				angle += 2.0 * kPi;
			return angle;
		}
	}

	double angle = kPi - fallbackBaseRotation;
	angle = std::fmod (angle, 2.0 * kPi);
	if (angle < 0.0)
		angle += 2.0 * kPi;
	return angle;
}

// Ověří, zda daný připojený prvek na konci trasy je samostatný MEP prvek (Terminal, Accessory, Fitting, Branch, Equipment, FlexibleSegment),
// ke kterému je bezpečné zavolat ConnectLogically bez rizika nechtěného sloučení dvou tras do jedné.
bool IsConnectableEndpointElement (const ACAPI::MEP::UniqueID& elemId)
{
	const API_Guid apiGuid = GSGuid2APIGuid (elemId.GetGuid ());
	API_Elem_Head head = {};
	head.guid = apiGuid;
	if (ACAPI_Element_GetHeader (&head) != NoError)
		return false;

	if (head.type.typeID != API_ExternalElemID)
		return false;

	const API_Guid& cid = head.type.classID;
	return IsTerminal (cid)
		|| IsAccessory (cid)
		|| IsFitting (cid)
		|| IsBranch (cid)
		|| IsEquipment (cid)
		|| IsFlexibleSegment (cid);
}

} // namespace


API_AnchorID RoutingElementReverser::MirrorAnchorLeftRight (API_AnchorID anchor)
{
	switch (anchor) {
		case APIAnc_LT: return APIAnc_RT;
		case APIAnc_RT: return APIAnc_LT;
		case APIAnc_LM: return APIAnc_RM;
		case APIAnc_RM: return APIAnc_LM;
		case APIAnc_LB: return APIAnc_RB;
		case APIAnc_RB: return APIAnc_LB;
		default:        return anchor;
	}
}


API_AnchorID RoutingElementReverser::MirrorAnchorTopBottom (API_AnchorID anchor)
{
	switch (anchor) {
		case APIAnc_LT: return APIAnc_LB;
		case APIAnc_LB: return APIAnc_LT;
		case APIAnc_MT: return APIAnc_MB;
		case APIAnc_MB: return APIAnc_MT;
		case APIAnc_RT: return APIAnc_RB;
		case APIAnc_RB: return APIAnc_RT;
		default:        return anchor;
	}
}


PreferredTransitionPlacement RoutingElementReverser::InvertTransitionPlacement (PreferredTransitionPlacement placement)
{
	return (placement == PreferredTransitionPlacement::SmallToLarge)
		? PreferredTransitionPlacement::LargeToSmall
		: PreferredTransitionPlacement::SmallToLarge;
}


API_Guid RoutingElementReverser::ResolveParentRoutingElement (const API_Guid& elemGuid, const API_Guid& classId)
{
	const Adapter::UniqueID uid (elemGuid);

	if (IsRoutingElement (classId)) {
		return elemGuid;
	}

	if (IsRigidSegment (classId)) {
		ACAPI::Result<RigidSegment> rigidSeg = RigidSegment::Get (uid);
		if (rigidSeg.IsOk ()) {
			ACAPI::Result<RoutingSegment> routSeg = RoutingSegment::Get (rigidSeg->GetRoutingSegmentId ());
			if (routSeg.IsOk ()) {
				return GSGuid2APIGuid (routSeg->GetRoutingElementId ().GetGuid ());
			}
		}
	} else if (IsElbow (classId)) {
		ACAPI::Result<Elbow> elbow = Elbow::Get (uid);
		if (elbow.IsOk ()) {
			ACAPI::Result<RoutingNode> routNode = RoutingNode::Get (elbow->GetRoutingNodeId ());
			if (routNode.IsOk ()) {
				return GSGuid2APIGuid (routNode->GetRoutingElementId ().GetGuid ());
			}
		}
	} else if (IsTransition (classId)) {
		ACAPI::Result<Transition> transition = Transition::Get (uid);
		if (transition.IsOk ()) {
			ACAPI::Result<RoutingNode> routNode = RoutingNode::Get (transition->GetRoutingNodeId ());
			if (routNode.IsOk ()) {
				return GSGuid2APIGuid (routNode->GetRoutingElementId ().GetGuid ());
			}
		}
	} else {
		// Zkusíme případ, kdy by GUID patřil přímo RoutingSegmentu nebo RoutingNode
		ACAPI::Result<RoutingSegment> routSeg = RoutingSegment::Get (uid);
		if (routSeg.IsOk ()) {
			return GSGuid2APIGuid (routSeg->GetRoutingElementId ().GetGuid ());
		}
		ACAPI::Result<RoutingNode> routNode = RoutingNode::Get (uid);
		if (routNode.IsOk ()) {
			return GSGuid2APIGuid (routNode->GetRoutingElementId ().GetGuid ());
		}
	}

	return APINULLGuid;
}


API_Guid RoutingElementReverser::Reverse (const API_Guid& routeGuid)
{
	Logger::DebugFmt ("Zahajuji otaceni RoutingElementu %s...", APIGuidToString (routeGuid).ToCStr ().Get ());

	ACAPI::Result<RoutingElement> routeRes = RoutingElement::Get (Adapter::UniqueID (routeGuid));
	if (routeRes.IsErr ()) {
		Logger::DebugFmt ("Chyba pri nacitani RoutingElementu %s: %s",
			APIGuidToString (routeGuid).ToCStr ().Get (),
			routeRes.UnwrapErr ().text.c_str ());
		return APINULLGuid;
	}

	RoutingElement& route = *routeRes;
	const std::vector<API_Coord3D> oldPolyLine              = route.GetPolyLine ();
	const std::vector<ACAPI::MEP::UniqueID> oldSegmentIds   = route.GetRoutingSegmentIds ();
	const std::vector<ACAPI::MEP::UniqueID> oldNodeIds      = route.GetRoutingNodeIds ();

	if (oldPolyLine.size () < 2 || oldSegmentIds.empty () || oldNodeIds.size () != oldPolyLine.size ()) {
		Logger::DebugFmt ("Neplatna geometrie trasy %s (pocet bodu=%zu, segmentu=%zu, uzlu=%zu).",
			APIGuidToString (routeGuid).ToCStr ().Get (),
			oldPolyLine.size (),
			oldSegmentIds.size (),
			oldNodeIds.size ());
		return APINULLGuid;
	}

	const size_t numNodes    = oldPolyLine.size ();
	const size_t numSegments = oldSegmentIds.size ();

	// 1. Záloha metadat trasy (vrstva, podlaží, rekonstrukce, klasifikace, vlastnosti, popisky)
	const ElementMetadataSnapshot routeMetadata = ElementMetadataHelper::Capture (routeGuid);

	// 2. Záloha napojení na koncích trasy
	std::vector<ACAPI::MEP::UniqueID> connectedEndElements;
	if (std::optional<ACAPI::MEP::UniqueID> beginPortId = route.GetLogicallyConnectedPortIdAtRouteBegin (); beginPortId.has_value ()) {
		if (ACAPI::Result<Port> p = Port::Get (*beginPortId); p.IsOk ()) {
			const ACAPI::MEP::UniqueID ownerId = p->GetMEPElementId ();
			if (IsConnectableEndpointElement (ownerId)) {
				connectedEndElements.push_back (ownerId);
			}
		}
	}
	if (std::optional<ACAPI::MEP::UniqueID> endPortId = route.GetLogicallyConnectedPortIdAtRouteEnd (); endPortId.has_value ()) {
		if (ACAPI::Result<Port> p = Port::Get (*endPortId); p.IsOk ()) {
			const ACAPI::MEP::UniqueID ownerId = p->GetMEPElementId ();
			if (IsConnectableEndpointElement (ownerId)) {
				connectedEndElements.push_back (ownerId);
			}
		}
	}

	// 3. Záloha výchozího nastavení trasy (RoutingElementDefault)
	RoutingElementDefault routeDefault = route.PickUpDefault ();
	RoutingSegmentDefault defaultSegParams = route.GetRoutingSegmentDefaultParameters ();
	RoutingNodeDefault    defaultNodeParams = route.GetRoutingNodeDefaultParameters ();

	// Kompenzace kotevního bodu, bočního odsazení a pořadí Koleno-Přechod ve výchozím nastavení trasy
	{
		const API_AnchorID mirroredDefaultAnchor = MirrorAnchorLeftRight (defaultSegParams.GetAnchor ());
		const double oldDefaultOffsetX = defaultSegParams.GetReferenceLineOffsetX ();
		const double newDefaultOffsetX = (std::abs (oldDefaultOffsetX) < 1e-12) ? 0.0 : -oldDefaultOffsetX;
		const PreferredTransitionPlacement invertedDefaultPlacement = InvertTransitionPlacement (defaultNodeParams.GetPreferredTransitionPlacement ());

		defaultSegParams.Modify ([&] (RoutingSegmentDefault::Modifier& mod) {
			mod.SetAnchor (mirroredDefaultAnchor);
			mod.SetReferenceLineOffsetX (newDefaultOffsetX);
		});

		defaultNodeParams.Modify ([&] (RoutingNodeDefault::Modifier& mod) {
			mod.SetPreferredTransitionPlacement (invertedDefaultPlacement);
		});

		routeDefault.Modify ([&] (RoutingElementDefault::Modifier& mod) {
			mod.SetRoutingSegmentDefault (defaultSegParams);
			mod.SetRoutingNodeDefault (defaultNodeParams);
		});
	}

	// 4. Záloha všech jednotlivých segmentů (S_0 ... S_{N-2}) a případných odboček (TakeOff)
	std::vector<SegmentSnapshot> oldSegments;
	oldSegments.reserve (numSegments);
	GS::Array<API_Guid> takeOffsToDelete;
	std::set<API_Guid> seenTakeOffGuids;

	for (size_t k = 0; k < numSegments; ++k) {
		ACAPI::Result<RoutingSegment> segRes = RoutingSegment::Get (oldSegmentIds[k]);
		if (segRes.IsErr ()) {
			Logger::DebugFmt ("Nelze nacist segment %zu trasy: %s", k, segRes.UnwrapErr ().text.c_str ());
			return APINULLGuid;
		}

		const RoutingSegment& seg = *segRes;
		const ConnectorShape shape = seg.GetCrossSectionShape ();
		std::optional<uint32_t> refId;
		if (shape == ConnectorShape::Circular) {
			if (ACAPI::Result<uint32_t> r = seg.GetCrossSectionReferenceId (); r.IsOk ()) {
				refId = *r;
			}
		}

		SegmentSnapshot snap {
			seg.PickUpDefault (),
			seg.GetRigidSegmentDefaultParameters (),
			shape,
			seg.GetCrossSectionWidth (),
			seg.GetCrossSectionHeight (),
			refId,
			seg.GetPreferenceTableId (),
			seg.GetAnchor (),
			seg.GetReferenceLineOffsetX (),
			seg.GetReferenceLineOffsetY (),
			seg.GetWallThickness (),
			seg.GetConnectionStyle (),
			seg.GetFlangeSize (),
			{}
		};

		// Zjistíme, zda jsou na RigidSegmentech tohoto segmentu připojené odbočky (TakeOff)
		const API_Coord3D& segBegin = oldPolyLine[k];
		const API_Coord3D& segEnd   = oldPolyLine[k + 1];
		const double segLen = Distance3D (segBegin, segEnd);
		const bool isVert = IsPurelyVerticalSegment (segBegin, segEnd);
		double oldRigidLen = -1.0;
		const auto oldRigidIds = seg.GetRigidSegmentIds ();
		if (!oldRigidIds.empty ()) {
			if (ACAPI::Result<RigidSegment> ors = RigidSegment::Get (oldRigidIds.front ()); ors.IsOk ()) {
				oldRigidLen = ors->GetLength ();
			}
		}

		Logger::DebugFmt ("  Segment[%zu]: (%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f), L=%.3f, rigidLen=%.3f, W=%.3f, H=%.3f, anchor=%d, offX=%.3f, offY=%.3f, vert=%d",
			k, segBegin.x, segBegin.y, segBegin.z, segEnd.x, segEnd.y, segEnd.z,
			segLen, oldRigidLen, snap.width, snap.height, static_cast<int> (snap.anchor), snap.offsetX, snap.offsetY, isVert ? 1 : 0);

		if (segLen > 1e-6) {
			const double ux = (segEnd.x - segBegin.x) / segLen;
			const double uy = (segEnd.y - segBegin.y) / segLen;
			const double uz = (segEnd.z - segBegin.z) / segLen;

			for (const ACAPI::MEP::UniqueID& rigidId : seg.GetRigidSegmentIds ()) {
				ACAPI::Result<RigidSegment> rigidRes = RigidSegment::Get (rigidId);
				if (rigidRes.IsErr ())
					continue;

				for (const ACAPI::MEP::UniqueID& connId : rigidRes->GetDirectConnectedMEPElementIds ()) {
					const API_Guid connGuid = GSGuid2APIGuid (connId.GetGuid ());
					if (seenTakeOffGuids.find (connGuid) != seenTakeOffGuids.end ())
						continue;

					ACAPI::Result<TakeOff> takeOffRes = TakeOff::Get (connId);
					if (takeOffRes.IsErr ())
						continue;

					seenTakeOffGuids.insert (connGuid);
					takeOffsToDelete.Push (connGuid);

					const API_Coord3D anchorPt = takeOffRes->GetAnchorPoint ();
					double distFromBegin = (anchorPt.x - segBegin.x) * ux
										 + (anchorPt.y - segBegin.y) * uy
										 + (anchorPt.z - segBegin.z) * uz;
					distFromBegin = std::clamp (distFromBegin, 0.0, segLen);

					// Pozn.: Nepoužívat takeOffRes->GetBaseRotation(), protože tato metoda byla do MEPAPI.dll
					// přidána až v Archicadu 29 Update 1 (build 3100) a v Archicadu 29.0.0 (build 3000)
					// by chybějící export znemožnil načtení doplňku. Úhel se plně určuje z 3D vektoru GetOrientation().
					const double newRot = ComputeTakeOffRotationOnReversedSegment (
						segEnd,
						segBegin,
						takeOffRes->GetOrientation ().direction,
						0.0
					);

					snap.takeOffs.push_back (TakeOffSnapshot {
						connGuid,
						takeOffRes->PickUpDefault (),
						takeOffRes->GetLength (),
						distFromBegin,
						newRot,
						ElementMetadataHelper::Capture (connGuid)
					});

					Logger::DebugFmt ("Zaznamenana odbocka (TakeOff) %s na segmentu %zu: d_old=%.3f m, L=%.3f m, newRot=%.1f deg",
						APIGuidToString (connGuid).ToCStr ().Get (),
						k,
						distFromBegin,
						segLen,
						newRot * 180.0 / kPi);
				}
			}
		}

		oldSegments.push_back ( std::move (snap));
	}

	// 5. Záloha všech uzlů (N_0 ... N_{N-1})
	std::vector<NodeSnapshot> oldNodes;
	oldNodes.reserve (numNodes);
	for (size_t j = 0; j < numNodes; ++j) {
		ACAPI::Result<RoutingNode> nodeRes = RoutingNode::Get (oldNodeIds[j]);
		if (nodeRes.IsErr ()) {
			Logger::DebugFmt ("Nelze nacist uzel %zu trasy: %s", j, nodeRes.UnwrapErr ().text.c_str ());
			return APINULLGuid;
		}

		const RoutingNode& node = *nodeRes;
		bool hasTrans = false;
		double transLen = 0.0;
		const std::vector<ACAPI::MEP::UniqueID> transIds = node.GetTransitionIds ();
		if (!transIds.empty ()) {
			if (ACAPI::Result<Transition> tr = Transition::Get (transIds.front ()); tr.IsOk ()) {
				hasTrans = true;
				transLen = tr->GetLength ();
				Logger::DebugFmt ("  Node[%zu] (%.3f, %.3f, %.3f): Transition L=%.3f, angle=%.1f, offY=%.3f, offZ=%.3f, elbows=%zu",
					j, oldPolyLine[j].x, oldPolyLine[j].y, oldPolyLine[j].z,
					transLen, tr->GetAngle (), tr->GetOffsetY (), tr->GetOffsetZ (), node.GetElbowIds ().size ());
			}
		} else {
			Logger::DebugFmt ("  Node[%zu] (%.3f, %.3f, %.3f): elbows=%zu",
				j, oldPolyLine[j].x, oldPolyLine[j].y, oldPolyLine[j].z, node.GetElbowIds ().size ());
		}

		oldNodes.push_back (NodeSnapshot {
			node.PickUpDefault (),
			node.GetElbowDefaultParameters (),
			node.GetTransitionDefaultParameters (),
			node.GetPreferredTransitionPlacement (),
			hasTrans,
			transLen
		});
	}

	// 6. Nativní in-place otočení směru trasy přes MEPElem::RoutingElement::ReverseDirection()
	//    Zachová všechny asociativní vazby (T-kusy / MEP::Branch, odbočky / TakeOff, navazující trasy)
	//    bez destruktivního smazání trasy (ACAPI_Element_Delete), které by v MEPLogicCore rozpojilo T-spoje.
	const NativeMEPSymbols& nativeSyms = NativeMEPSymbols::Get ();
	if (nativeSyms.IsAvailable ()) {
		const void* routeRefPtr = NativeODBRefAccessor::GetODBRefPtr (route);
		if (routeRefPtr != nullptr) {
			ACAPI::Result<void> inPlaceRes = route.Modify ([&] (RoutingElement::Modifier& mod) -> GSErrCode {
				// 6a. Kompenzace výchozích parametrů segmentů a uzlů na úrovni trasy
				mod.SetRoutingSegmentDefaultParameters (defaultSegParams);
				mod.SetRoutingNodeDefaultParameters (defaultNodeParams);

				// 6b. Nativní in-place otočení směru (prohodí BeginNode <-> EndNode segmentů,
				//     zavolá SwapSplitPointReferenceNode pro odbočky a prohodí koncové porty Begin <-> End)
				void* rawRoute = nativeSyms.processModificationRequest (routeRefPtr);
				if (rawRoute == nullptr) {
					return Error;
				}
				nativeSyms.reverseDirection (rawRoute);

				// 6c. Kompenzace kotevního bodu a odsazení jednotlivých segmentů
				for (size_t k = 0; k < numSegments; ++k) {
					ACAPI::Result<RoutingSegment> segRes = RoutingSegment::Get (oldSegmentIds[k]);
					if (segRes.IsErr ())
						continue;

					const void* segRefPtr = NativeODBRefAccessor::GetODBRefPtr (*segRes);
					void* rawSeg = (segRefPtr != nullptr) ? nativeSyms.processModificationRequest (segRefPtr) : nullptr;
					if (rawSeg == nullptr)
						continue;

					const SegmentSnapshot& oldSeg = oldSegments[k];
					const bool isPurelyVertical = IsPurelyVerticalSegment (oldPolyLine[k], oldPolyLine[k + 1]);
					API_AnchorID newAnchor = MirrorAnchorLeftRight (oldSeg.anchor);
					const double newOffsetX = (std::abs (oldSeg.offsetX) < 1e-12) ? 0.0 : -oldSeg.offsetX;
					double newOffsetY = oldSeg.offsetY;

					if (isPurelyVertical) {
						newAnchor  = MirrorAnchorTopBottom (newAnchor);
						newOffsetY = (std::abs (oldSeg.offsetY) < 1e-12) ? 0.0 : -oldSeg.offsetY;
					}

					nativeSyms.setSegmentAnchor (rawSeg, static_cast<int> (newAnchor));
					nativeSyms.setSegmentOffsetX (rawSeg, newOffsetX);
					nativeSyms.setSegmentOffsetY (rawSeg, newOffsetY);
				}

				// 6d. Kompenzace pořadí Koleno-Přechod na jednotlivých uzlech
				for (size_t j = 0; j < numNodes; ++j) {
					ACAPI::Result<RoutingNode> nodeRes = RoutingNode::Get (oldNodeIds[j]);
					if (nodeRes.IsErr ())
						continue;

					const void* nodeRefPtr = NativeODBRefAccessor::GetODBRefPtr (*nodeRes);
					void* rawNode = (nodeRefPtr != nullptr) ? nativeSyms.processModificationRequest (nodeRefPtr) : nullptr;
					if (rawNode == nullptr)
						continue;

					const PreferredTransitionPlacement invertedPlacement =
						InvertTransitionPlacement (oldNodes[j].preferredTransitionPlacement);
					nativeSyms.setNodeTransitionPlacement (rawNode, static_cast<int> (invertedPlacement));
				}

				// 6e. Přepočet napojení v koncových uzlech (T-kusy / Branch) a přegenerování podprvků trasy
				const void* idSpacePtr = GetODBRefIdSpace (routeRefPtr);
				if (nativeSyms.updateConnectionAtNode != nullptr && idSpacePtr != nullptr) {
					if (ACAPI::Result<RoutingNode> newFirstNode = RoutingNode::Get (oldNodeIds.back ()); newFirstNode.IsOk ()) {
						if (const void* fnRef = NativeODBRefAccessor::GetODBRefPtr (*newFirstNode); fnRef != nullptr) {
							nativeSyms.updateConnectionAtNode (fnRef, idSpacePtr);
						}
					}
					if (ACAPI::Result<RoutingNode> newLastNode = RoutingNode::Get (oldNodeIds.front ()); newLastNode.IsOk ()) {
						if (const void* lnRef = NativeODBRefAccessor::GetODBRefPtr (*newLastNode); lnRef != nullptr) {
							nativeSyms.updateConnectionAtNode (lnRef, idSpacePtr);
						}
					}
				}
				nativeSyms.updateRoutingElement (routeRefPtr);

				return NoError;
			}, "Otoceni smeru TZB trasy");

			if (inPlaceRes.IsOk ()) {
				const std::vector<API_Coord3D> finalPolyLine = route.GetPolyLine ();
				const bool beginConn = route.GetLogicallyConnectedPortIdAtRouteBegin ().has_value ();
				const bool endConn   = route.GetLogicallyConnectedPortIdAtRouteEnd ().has_value ();
				Logger::DebugFmt ("RoutingElement %s uspesne otocen in-place (%zu vrcholu, %zu segmentu, beginConn=%d, endConn=%d). Finalni vrcholy a delky segmentu:",
					APIGuidToString (routeGuid).ToCStr ().Get (),
					finalPolyLine.size (),
					numSegments,
					beginConn ? 1 : 0,
					endConn ? 1 : 0);
				for (size_t j = 0; j < finalPolyLine.size (); ++j) {
					Logger::DebugFmt ("  FinalNode[%zu]: (%.3f, %.3f, %.3f)",
						j, finalPolyLine[j].x, finalPolyLine[j].y, finalPolyLine[j].z);
				}
				const std::vector<ACAPI::MEP::UniqueID> finalSegIds = route.GetRoutingSegmentIds ();
				for (size_t k = 0; k < finalSegIds.size (); ++k) {
					if (ACAPI::Result<RoutingSegment> fs = RoutingSegment::Get (finalSegIds[k]); fs.IsOk ()) {
						double rigidLen = -1.0;
						const auto rIds = fs->GetRigidSegmentIds ();
						if (!rIds.empty ()) {
							if (ACAPI::Result<RigidSegment> rs = RigidSegment::Get (rIds.front ()); rs.IsOk ()) {
								rigidLen = rs->GetLength ();
							}
						}
						Logger::DebugFmt ("  FinalSegment[%zu]: anchor=%d, rigidLen=%.3f m",
							k, static_cast<int> (fs->GetAnchor ()), rigidLen);
					}
				}
				return routeGuid;
			} else {
				Logger::DebugFmt ("In-place otoceni trasy %s vratilo chybu: %s, pouziji nouzovy rezim Place.",
					APIGuidToString (routeGuid).ToCStr ().Get (),
					inPlaceRes.UnwrapErr ().text.c_str ());
			}
		}
	}

	// 7. Nouzový režim: Sestavení otočené 3D lomené čáry a map průřezů pro nové segmenty
	std::vector<API_Coord3D> reversedPolyLine (oldPolyLine.rbegin (), oldPolyLine.rend ());
	std::map<UInt32, RoutingSegmentRectangularCrossSectionData> rectCrossSectionMap;
	std::map<UInt32, RoutingSegmentCircularCrossSectionData>    circCrossSectionMap;

	for (size_t newIdx = 0; newIdx < numSegments; ++newIdx) {
		const size_t oldIdx = numSegments - 1 - newIdx;
		const SegmentSnapshot& oldSeg = oldSegments[oldIdx];
		const UInt32 segIndexKey = static_cast<UInt32> (newIdx);

		if (oldSeg.shape == ConnectorShape::Circular && oldSeg.referenceId.has_value ()) {
			circCrossSectionMap.emplace (segIndexKey, RoutingSegmentCircularCrossSectionData (*oldSeg.referenceId));
		} else {
			rectCrossSectionMap.emplace (segIndexKey, RoutingSegmentRectangularCrossSectionData (oldSeg.width, oldSeg.height));
		}
	}

	// 7. Smazání původních odboček a původní trasy
	if (!takeOffsToDelete.IsEmpty ()) {
		ACAPI_Element_Delete (takeOffsToDelete);
	}

	GS::Array<API_Guid> routeToDelete;
	routeToDelete.Push (routeGuid);
	GSErrCode delErr = ACAPI_Element_Delete (routeToDelete);
	if (delErr != NoError) {
		Logger::DebugFmt ("Smazani puvodni trasy %s selhalo s chybou %d.",
			APIGuidToString (routeGuid).ToCStr ().Get (),
			static_cast<int> (delErr));
		return APINULLGuid;
	}

	// 8. Vložení otočené trasy se zachováním původního API_Guid
	ACAPI::Result<ACAPI::MEP::UniqueID> placedRouteIdRes = routeDefault.Place (
		reversedPolyLine,
		rectCrossSectionMap,
		circCrossSectionMap,
		routeGuid
	);

	if (placedRouteIdRes.IsErr ()) {
		Logger::DebugFmt ("Umisteni otocene trasy selhalo: %s", placedRouteIdRes.UnwrapErr ().text.c_str ());
		return APINULLGuid;
	}

	const ACAPI::MEP::UniqueID newRouteUniqueId = *placedRouteIdRes;
	const API_Guid newRouteGuid = GSGuid2APIGuid (newRouteUniqueId.GetGuid ());

	ACAPI::Result<RoutingElement> newRouteRes = RoutingElement::Get (newRouteUniqueId);
	if (newRouteRes.IsErr ()) {
		Logger::DebugFmt ("Nelze nacist nove umistenou trasu: %s", newRouteRes.UnwrapErr ().text.c_str ());
		return APINULLGuid;
	}

	RoutingElement& newRoute = *newRouteRes;

	// 9. Aplikace individuálních nastavení segmentů v opačném pořadí (S'_k <- S_{N-2-k})
	const std::vector<ACAPI::MEP::UniqueID> newSegmentIds = newRoute.GetRoutingSegmentIds ();
	for (size_t newIdx = 0; newIdx < newSegmentIds.size () && newIdx < numSegments; ++newIdx) {
		const size_t oldIdx = numSegments - 1 - newIdx;
		SegmentSnapshot& oldSeg = oldSegments[oldIdx];

		const bool isPurelyVertical = IsPurelyVerticalSegment (reversedPolyLine[newIdx], reversedPolyLine[newIdx + 1]);
		API_AnchorID newAnchor = MirrorAnchorLeftRight (oldSeg.anchor);
		const double newOffsetX = (std::abs (oldSeg.offsetX) < 1e-12) ? 0.0 : -oldSeg.offsetX;
		double newOffsetY = oldSeg.offsetY;

		// U ryze svislého segmentu (dx == 0, dy == 0, dz != 0) dochází při otočení směru (nahoru <-> dolů)
		// také k převrácení lokální osy Y průřezu (Top <-> Bottom).
		if (isPurelyVertical) {
			newAnchor  = MirrorAnchorTopBottom (newAnchor);
			newOffsetY = (std::abs (oldSeg.offsetY) < 1e-12) ? 0.0 : -oldSeg.offsetY;
		}

		oldSeg.segDefault.Modify ([&] (RoutingSegmentDefault::Modifier& mod) {
			mod.SetAnchor (newAnchor);
			mod.SetReferenceLineOffsetX (newOffsetX);
			mod.SetReferenceLineOffsetY (newOffsetY);
		});

		ACAPI::Result<RoutingSegment> newSegRes = RoutingSegment::Get (newSegmentIds[newIdx]);
		if (newSegRes.IsOk ()) {
			newSegRes->Modify ([&] (RoutingSegment::Modifier& mod) {
				mod.Inject (oldSeg.segDefault);
				mod.SetPreferenceTableId (oldSeg.preferenceTableId);
				mod.SetCrossSectionShape (oldSeg.shape);
				if (oldSeg.shape == ConnectorShape::Circular && oldSeg.referenceId.has_value ()) {
					mod.SetCrossSectionReferenceId (*oldSeg.referenceId);
				} else {
					mod.SetCrossSectionWidth (oldSeg.width);
					mod.SetCrossSectionHeight (oldSeg.height);
				}
				mod.SetAnchor (newAnchor);
				mod.SetReferenceLineOffsetX (newOffsetX);
				mod.SetReferenceLineOffsetY (newOffsetY);
				mod.SetWallThickness (oldSeg.wallThickness);
				mod.SetConnectionStyle (oldSeg.connectionStyle);
				if (oldSeg.connectionStyle == ConnectionStyle::Flanged && oldSeg.flangeSize.has_value ()) {
					mod.SetFlangeSize (*oldSeg.flangeSize);
				}
				mod.SetRigidSegmentDefaultParameters (oldSeg.rigidDefault);
			}, "Obnova nastaveni segmentu trasy");
		}
	}

	// 10. Aplikace individuálních nastavení uzlů v opačném pořadí (N'_j <- N_{N-1-j})
	//     Při otočení směru trasy se prohodí příchozí (incoming) a odchozí (outgoing) segment uzlu.
	//     Aby přechod (Transition) zůstal na totožné straně kolena (Elbow) jako před otočením,
	//     musí se pravidlo PreferredTransitionPlacement invertovat (SmallToLarge <-> LargeToSmall).
	const std::vector<ACAPI::MEP::UniqueID> newNodeIds = newRoute.GetRoutingNodeIds ();
	for (size_t newIdx = 0; newIdx < newNodeIds.size () && newIdx < numNodes; ++newIdx) {
		const size_t oldIdx = numNodes - 1 - newIdx;
		NodeSnapshot& oldNode = oldNodes[oldIdx];
		const PreferredTransitionPlacement invertedPlacement = InvertTransitionPlacement (oldNode.preferredTransitionPlacement);

		oldNode.nodeDefault.Modify ([&] (RoutingNodeDefault::Modifier& mod) {
			mod.SetPreferredTransitionPlacement (invertedPlacement);
		});

		ACAPI::Result<RoutingNode> newNodeRes = RoutingNode::Get (newNodeIds[newIdx]);
		if (newNodeRes.IsOk ()) {
			newNodeRes->Modify ([&] (RoutingNode::Modifier& mod) {
				mod.Inject (oldNode.nodeDefault);
				mod.SetPreferredTransitionPlacement (invertedPlacement);
				mod.SetElbowDefaultParameters (oldNode.elbowDefault);
				mod.SetTransitionDefaultParameters (oldNode.transitionDefault);
			}, "Obnova nastaveni uzlu trasy");
		}
	}

	// 11. Znovuosazení odboček (TakeOff) na otočené segmenty
	const std::vector<ACAPI::MEP::UniqueID> refreshedSegmentIds = newRoute.GetRoutingSegmentIds ();
	for (size_t newIdx = 0; newIdx < refreshedSegmentIds.size () && newIdx < numSegments; ++newIdx) {
		const size_t oldIdx = numSegments - 1 - newIdx;
		const SegmentSnapshot& oldSeg = oldSegments[oldIdx];
		if (oldSeg.takeOffs.empty ())
			continue;

		const double segLen = Distance3D (reversedPolyLine[newIdx], reversedPolyLine[newIdx + 1]);
		for (const TakeOffSnapshot& toSnap : oldSeg.takeOffs) {
			const double newDist = std::clamp (segLen - toSnap.distanceFromOldBegin, 0.0, segLen);
			ACAPI::Result<ACAPI::MEP::UniqueID> placedTakeOffId = toSnap.takeOffDefault.Place (
				refreshedSegmentIds[newIdx],
				newDist,
				toSnap.newRotationAroundAxis,
				toSnap.originalGuid
			);

			if (placedTakeOffId.IsOk ()) {
				if (ACAPI::Result<TakeOff> placedTakeOff = TakeOff::Get (*placedTakeOffId); placedTakeOff.IsOk ()) {
					placedTakeOff->Modify ([&] (TakeOff::Modifier& mod) {
						mod.SetLength (toSnap.length);
					}, "Obnova delky odbocky");
				}
				ElementMetadataHelper::Restore (GSGuid2APIGuid (placedTakeOffId->GetGuid ()), toSnap.metadata);
			} else {
				Logger::DebugFmt ("Upozorneni: Nepodarilo se znovu osadit odbocku %s: %s",
					APIGuidToString (toSnap.originalGuid).ToCStr ().Get (),
					placedTakeOffId.UnwrapErr ().text.c_str ());
			}
		}
	}

	// 12. Obnova logických napojení na koncích trasy
	for (const ACAPI::MEP::UniqueID& endElemId : connectedEndElements) {
		newRoute.Modify ([&] (RoutingElement::Modifier& mod) -> GSErrCode {
			mod.ConnectLogically (endElemId);
			return NoError;
		}, "Obnova napojeni konce trasy");
	}

	// 13. Obnova metadat trasy (vrstva, podlaží, stav rekonstrukce, klasifikace, vlastnosti, popisky)
	ElementMetadataHelper::Restore (newRouteGuid, routeMetadata);

	const std::vector<API_Coord3D> finalPolyLine = newRoute.GetPolyLine ();
	Logger::DebugFmt ("RoutingElement %s uspesne otocen (%zu vrcholu, %zu segmentu). Finalni vrcholy a delky segmentu:",
		APIGuidToString (newRouteGuid).ToCStr ().Get (),
		finalPolyLine.size (),
		numSegments);
	for (size_t j = 0; j < finalPolyLine.size (); ++j) {
		Logger::DebugFmt ("  FinalNode[%zu]: (%.3f, %.3f, %.3f)",
			j, finalPolyLine[j].x, finalPolyLine[j].y, finalPolyLine[j].z);
	}
	const std::vector<ACAPI::MEP::UniqueID> finalSegIds = newRoute.GetRoutingSegmentIds ();
	for (size_t k = 0; k < finalSegIds.size (); ++k) {
		if (ACAPI::Result<RoutingSegment> fs = RoutingSegment::Get (finalSegIds[k]); fs.IsOk ()) {
			double rigidLen = -1.0;
			const auto rIds = fs->GetRigidSegmentIds ();
			if (!rIds.empty ()) {
				if (ACAPI::Result<RigidSegment> rs = RigidSegment::Get (rIds.front ()); rs.IsOk ()) {
					rigidLen = rs->GetLength ();
				}
			}
			Logger::DebugFmt ("  FinalSegment[%zu]: anchor=%d, rigidLen=%.3f m",
				k, static_cast<int> (fs->GetAnchor ()), rigidLen);
		}
	}

	return newRouteGuid;
}

} // namespace OtocSmerTZB
