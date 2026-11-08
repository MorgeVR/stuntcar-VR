#ifndef	_STUNT_CAR_RACER
#define	_STUNT_CAR_RACER

#include "scr_compat.h"

/*	========= */
/*	Constants */
/*	========= */
#define SCR_BASE_COLOUR	26

typedef enum
	{
	TRACK_MENU = 0,
	TRACK_PREVIEW,
	GAME_IN_PROGRESS,
	GAME_OVER
	} GameModeType;

extern GameModeType GameMode;

/*	============================== */
/*	External function declarations */
/*	============================== */
extern void GetScreenDimensions( int32_t *screen_width,
								 int32_t *screen_height );

extern DWORD SCRGB (int32_t colour_index);
extern DWORD SCColour (int32_t colour_index);

extern void SetSolidColour (int32_t colour_index);
extern void SetLineColour (int32_t colour_index);
extern void SetTextureColour (int32_t colour_index);

// Debug
extern int32_t VALUE1, VALUE2, VALUE3;

#endif	/* _STUNT_CAR_RACER */
