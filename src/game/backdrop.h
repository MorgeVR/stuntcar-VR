
#ifndef	_BACKDROP
#define	_BACKDROP

#include "scr_compat.h"

/*	========= */
/*	Constants */
/*	========= */

/*	===================== */
/*	Structure definitions */
/*	===================== */

/*	============================== */
/*	External function declarations */
/*	============================== */
extern void DrawBackdrop( int32_t viewpoint_y,
						  int32_t viewpoint_x_angle,
						  int32_t viewpoint_y_angle,
						  int32_t viewpoint_z_angle );

extern void NextSceneryType( void );

extern int32_t GetSceneryType( void );

// 3D version of the scenery ring (see backdrop.cpp)
extern int32_t BuildSceneryMesh( float centre_x, float centre_z, float radius,
								 UTVERTEX *out_vertices, int32_t max_vertices );

#endif	/* _BACKDROP */
