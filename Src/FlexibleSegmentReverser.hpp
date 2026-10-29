#ifndef FLEXIBLE_SEGMENT_REVERSER_HPP
#define FLEXIBLE_SEGMENT_REVERSER_HPP

#include "ACAPinc.h"
#include "API_Guid.hpp"

namespace OtocSmerTZB {

class FlexibleSegmentReverser {
public:
	// Otočí směr flexibilního potrubí / hadice (Spline nebo Polyline FlexibleSegment).
	// Musí být voláno uvnitř otevřené Undo transakce.
	static bool Reverse (const API_Guid& flexGuid);
};

} // namespace OtocSmerTZB

#endif // FLEXIBLE_SEGMENT_REVERSER_HPP
