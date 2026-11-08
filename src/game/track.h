
#ifndef	_TRACK
#define	_TRACK

/*	============= */
/*	Include files */
/*	============= */
#include "scr_compat.h"
#include "3d_engine.h"

/*	========= */
/*	Constants */
/*	========= */
#define	PC_FACTOR	2		// 24/04/1998

#define	MAX_PIECES_PER_TRACK	100

#define	MAX_SEGMENTS_PER_PIECE	13

#define	NUM_TRACK_CUBES	16		// x,z dimensions of Track, in cubes

#define	CUBE_SIZE	0x04000000		// size of a single track cube
									// (0x800 * PC_FACTOR of 2 * PRECISION)
#define	LOG_CUBE_SIZE	26			// to base 2

#define	TRACK_BOTTOM_Y	0		// used to be 512

// note that track numbers do not correspond to track league positions
#define	NO_TRACK		-1
#define	LITTLE_RAMP		0
#define	STEPPING_STONES	1
#define	HUMP_BACK		2
#define	BIG_RAMP		3
#define	SKI_JUMP		4
#define	DRAW_BRIDGE		5
#define	HIGH_JUMP		6
#define	ROLLER_COASTER	7
#define	NUM_TRACKS		8

/*	===================== */
/*	Structure definitions */
/*	===================== */
typedef struct
	{
	int32_t	x;
	int32_t	z;
	} COORD_XZ;

typedef struct
	{
	int32_t	y;
	} COORD_Y;

typedef struct
	{
	int32_t	x, y, z;		// of piece's front left corner, within world
	int32_t	roughPieceAngle;	// 0, 90, 180, 270 degrees (stored in internal angle format)
	int32_t	oppositeDirection;	// TRUE/FALSE - normal travel direction along piece
	int32_t	curveToLeft;	// TRUE/FALSE
	int32_t	type;			// near.section.byte1 - 0x00 STRAIGHT, 0x40 DIAGONAL (45 degrees)
							//						0x80 CURVE RIGHT, 0xc0 CURVE LEFT
	int32_t	lengthReduction;
	int32_t	steeringAmount;
	/* following not needed since project converted to use Direct3D
	int32_t	visible;		// TRUE/FALSE - from current viewpoint
	int32_t    distant;        // TRUE/FALSE - from current viewpoint (using DISTANT_OBJECT_BOUNDARY)
	int32_t	zClipNeeded;	// TRUE/FALSE - from current viewpoint
	*/
	int32_t	numSegments;
	int32_t	firstSegment;	// piece's first segment number within the whole track
	/* following not needed since project converted to use Direct3D
	int32_t	minZDifference;	// used to decide order of segment drawing
	*/
	int32_t	initialColour;	// initial roadLinesColour offset (0 or 1)
	BYTE	roadColour[MAX_SEGMENTS_PER_PIECE];		// for individual segment road surfaces
	BYTE	sidesColour;
	COORD_3D *coords;		// unrotated
	int32_t	coordsSize;		// allocated memory size for coords
	/* following not needed since project converted to use Direct3D
	COORD_3D *transformed_coords;
	COORD_2D *screen_coords;
	*/
	} TRACK_PIECE;

/*	============================== */
/*	External function declarations */
/*	============================== */
extern WCHAR *GetTrackName( int32_t track );

extern char GetPieceAngleAndTemplate( int32_t piece );

extern int32_t ConvertAmigaTrack( int32_t track );

extern void FreeTrackData( void );

// Mesh output (replaces the Direct3D vertex buffers)
extern HRESULT BuildTrackMesh (void);		// call after ConvertAmigaTrack
extern const UTVERTEX *GetTrackVertices (int32_t *count);
extern uint32_t GetTrackMeshVersion (void);	// changes whenever vertices change (Draw Bridge)
extern const UTVERTEX *GetShadowVertices (int32_t *count);	// opponent's shadow triangles
extern void RemoveShadowTriangles (void);

extern void MoveDrawBridge ( void );

extern void ResetDrawBridge( void );

#endif	/* _TRACK */
