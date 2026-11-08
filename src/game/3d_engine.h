
#ifndef	_3D_ENGINE
#define	_3D_ENGINE

#include "scr_compat.h"

/*	========= */
/*	Constants */
/*	========= */
#define	PI			3.1415926535898

#define	MAX_ANGLE	65536	// value that is equivalent to 360 degrees

#define	_360_DEGREES	(MAX_ANGLE)
#define	_270_DEGREES	(3*MAX_ANGLE/4)
#define	_180_DEGREES	(MAX_ANGLE/2)
#define	_90_DEGREES		(MAX_ANGLE/4)
#define	_0_DEGREES		(0)


// could use enumerated type for the following
#define	X_X_COMP	0		// rotated x components
#define	X_Y_COMP	1
#define	X_Z_COMP	2

#define	Y_X_COMP	3		// rotated y components
#define	Y_Y_COMP	4
#define	Y_Z_COMP	5

#define	Z_X_COMP	6		// rotated z components
#define	Z_Y_COMP	7
#define	Z_Z_COMP	8

#define	NUM_TRIG_COEFFS		Z_Z_COMP + 1


#define	PRECISION	16384
#define	LOG_PRECISION	14	// to base 2

#define	MAX_POLY_SIDES	8

#define	MAX_COORDS	200

//#define	FOCUS	256			// for screen width of 320
//#define	LOG_FOCUS	8		// to base 2

#define	FOCUS	512			// for screen width of 640
#define	LOG_FOCUS	9		// to base 2

/*	===================== */
/*	Structure definitions */
/*	===================== */
typedef struct
	{
	int32_t	x;
	int32_t	y;
	int32_t	z;
	} COORD_3D;

typedef struct
	{
	int32_t	x;
	int32_t	y;
	} COORD_2D;

/*	============================== */
/*	External function declarations */
/*	============================== */
extern void CreateSinCosTable( void );

extern void GetSinCos( int32_t angle,
					   short *sin,
					   short *cos );

extern void SetWorldOffset( int32_t x_offset,
							int32_t y_offset,
							int32_t z_offset );

extern void SetCoords( COORD_3D *tptr,
					   COORD_2D *sptr );

extern void DefaultCoords( void );

extern void CalcYXZTrigCoefficients( int32_t x_angle,
									 int32_t y_angle,
									 int32_t z_angle );

extern short *TrigCoefficients( void );

extern void RotateCoordinate( int32_t *xptr,
							  int32_t *yptr,
							  int32_t *zptr );

extern void WorldOffset( int32_t *xptr,
					     int32_t *yptr,
					     int32_t *zptr );

extern int32_t TransformCoordinates( COORD_3D *cptr,
								  int32_t size );

extern int32_t TransformedZ( int32_t offset );

extern int32_t TexturedPolygon( int32_t *cptr,
							 int32_t sides,
							 int32_t *vptr );

extern int32_t PolygonVisible( int32_t *cptr );

extern int32_t Polygon( int32_t *cptr,
					 int32_t sides );

extern int32_t PolygonEx( int32_t *cptr,
					   int32_t sides,
					   int32_t *optr );

extern void Line( int32_t c1,
				  int32_t c2 );

extern int32_t PolygonZClipped( int32_t *cptr,
							 int32_t sides,
							 int32_t check_orientation,
							 int32_t *on_screen );

extern void LineZClipped( int32_t c1,
						  int32_t c2 );

extern void ZClip( COORD_3D *below,
				   COORD_3D *above,
				   int32_t screen_width,
				   int32_t screen_height,
				   int32_t *x,
				   int32_t *y );

extern void LockViewpointToTarget( int32_t viewpoint_x,
								   int32_t viewpoint_y,
								   int32_t viewpoint_z,
								   int32_t target_x,
								   int32_t target_y,
								   int32_t target_z,
								   int32_t *viewpoint_x_angle,
								   int32_t *viewpoint_y_angle );

extern void DrawPolygon( POINT *pptr,
						 int32_t sides );

extern void DrawFilledRectangle( int32_t x1, int32_t y1, int32_t x2, int32_t y2, DWORD colour );

// 2D drawing sinks (screen space, original 640x480 co-ordinates). Optional.
typedef void (*ScrPolygon2DSink)( const POINT *points, int32_t sides, DWORD colour );
typedef void (*ScrRect2DSink)( int32_t x1, int32_t y1, int32_t x2, int32_t y2, DWORD colour );
extern ScrPolygon2DSink g_scrPolygon2DSink;
extern ScrRect2DSink    g_scrRect2DSink;

#endif	/* _3D_ENGINE */
