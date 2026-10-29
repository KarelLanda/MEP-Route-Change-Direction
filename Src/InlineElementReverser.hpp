#ifndef INLINE_ELEMENT_REVERSER_HPP
#define INLINE_ELEMENT_REVERSER_HPP

#include "ACAPinc.h"
#include "API_Guid.hpp"

namespace OtocSmerTZB {

enum class InlineReverseStatus {
	Success,
	NotTwoPortElement,
	Error
};

class InlineElementReverser {
public:
	// Otočí směr 2-portového vloženého prvku v trase (Accessory - armatura/klapka, nebo Fitting - tvarovka).
	// Zachová horní orientaci prvku (rotation) a přesnou středovou polohu mezi porty v 3D prostoru.
	static InlineReverseStatus Reverse (const API_Guid& elemGuid);
};

} // namespace OtocSmerTZB

#endif // INLINE_ELEMENT_REVERSER_HPP
