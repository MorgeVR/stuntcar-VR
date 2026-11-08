
#ifndef	_CAR
#define	_CAR

#include "scr_compat.h"

/*	========= */
/*	Constants */
/*	========= */
// VCAR is short for VISIBLE_CAR
#define	VCAR_WIDTH	162		// ((width 27+27 * segment width 384) / surface factor 256) * PC_FACTOR
#define	VCAR_LENGTH	256		// ((length 128 * segment length 256) / surface factor 256) * PC_FACTOR
#define	VCAR_HEIGHT	162		// chosen to look ok with the above

/*	===================== */
/*	Structure definitions */
/*	===================== */

/*	============================== */
/*	External function declarations */
/*	============================== */
extern HRESULT BuildCarMesh (void);
extern const UTVERTEX *GetCarVertices (int32_t *count);	// car-local game units

#endif	/* _CAR */
