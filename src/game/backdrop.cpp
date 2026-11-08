/**************************************************************************

    Backdrop.cpp - Functions for manipulating backdrop

 **************************************************************************/

/*	============= */
/*	Include files */
/*	============= */
#include "scr_compat.h"

#include "stuntcarracer.h"
#include "backdrop.h"
#include "3d_engine.h"

/*	===== */
/*	Debug */
/*	===== */
#if defined(DEBUG) || defined(_DEBUG)
extern FILE *out;
#endif

/*	========= */
/*	Constants */
/*	========= */
#define SKY_COLOUR		(SCR_BASE_COLOUR + 7)
#define GROUND_COLOUR	(SCR_BASE_COLOUR + 13)

// Scenery type ranges 0-4
#define	MIN_SCENERY_TYPE	0
#define	MAX_SCENERY_TYPE	4

/*	=========== */
/*	Static data */
/*	=========== */
static int32_t current_scenery_type = MAX_SCENERY_TYPE;

/*	===================== */
/*	Function declarations */
/*	===================== */
static void DrawHorizon( int32_t viewpoint_y,
						 int32_t viewpoint_x_angle,
						 int32_t viewpoint_z_angle );

static void DrawScenery( int32_t viewpoint_y,
						 int32_t viewpoint_x_angle,
						 int32_t viewpoint_y_angle,
						 int32_t viewpoint_z_angle );

static int32_t ClipLine( int32_t *x1ptr,
					  int32_t *y1ptr,
					  int32_t *x2ptr,
					  int32_t *y2ptr,
					  int32_t screen_width,
					  int32_t screen_height );

/*	======================================================================================= */
/*	Function:		DrawBackdrop															*/
/*																							*/
/*	Description:	Draw the backdrop using the supplied viewpoint							*/
/*	======================================================================================= */

void DrawBackdrop( int32_t viewpoint_y,
				   int32_t viewpoint_x_angle,
				   int32_t viewpoint_y_angle,
				   int32_t viewpoint_z_angle )
	{
	DrawHorizon(viewpoint_y,
				viewpoint_x_angle,
				viewpoint_z_angle);

	DrawScenery(viewpoint_y,
				viewpoint_x_angle,
				viewpoint_y_angle,
				viewpoint_z_angle);
	}

/*	======================================================================================= */
/*	Function:		NextSceneryType															*/
/*																							*/
/*	Description:	Change to the next type of scenery										*/
/*	======================================================================================= */

void NextSceneryType( void )
	{
	current_scenery_type++;

	if (current_scenery_type > MAX_SCENERY_TYPE)
		current_scenery_type = MIN_SCENERY_TYPE;
	}

/*	======================================================================================= */
/*	Function:		DrawHorizon																*/
/*																							*/
/*	Description:	Draw the horizon using the supplied viewpoint							*/
/*					- fills the entire screen with ground/sky colours						*/
/*	======================================================================================= */

// should only be able to see as far as horizon,
// so if track is further away then either :-
//
//		horizon should also be further away
//		OR
//		shouldn't be drawing pieces at this distance
//
// - viewpoint limitations should eventually prevent track exceeding horizon

static void DrawHorizon( int32_t viewpoint_y,
						 int32_t viewpoint_x_angle,
						 int32_t viewpoint_z_angle )
	{
	int32_t i;
	int32_t upside_down = FALSE;
	short sin_x, sin_z;
	short cos_x, cos_z;
	int32_t x, y, z, y_adjust;
	int32_t trans_x, trans_y, trans_z;
	int32_t screen_width, screen_height;
	int32_t x1, y1, x2, y2, xs, xl, ys, yl;

	// set up two co-ordinates defining horizon line
	COORD_3D plane[2];
	COORD_2D screen_coords[2];

	// left co-ordinate
	plane[0].x = -0x00010000;
	plane[0].y = 0;
	plane[0].z = 0x00010000;

	// right co-ordinate
	plane[1].x = 0x00010000;
	plane[1].y = 0;
	plane[1].z = 0x00010000;


	// start of code
	GetScreenDimensions(&screen_width, &screen_height);

	// calculate y adjustment depending upon viewpoint_x_angle.
	// this is needed because only two horizon points are used rather than four.
	// when the rotated z values are negative (i.e. viewpoint_x_angle in range shown)
	// the resulting y values are negated so the y adjustment must also change sign
	if ((viewpoint_x_angle >= (MAX_ANGLE/4)) &&
		(viewpoint_x_angle < ((3*MAX_ANGLE)/4)))	// if >= 90 and less than 270 degrees
		y_adjust = (viewpoint_y >> LOG_PRECISION);
	else
		y_adjust = -(viewpoint_y >> LOG_PRECISION);

	y_adjust /= 2;	// 24/04/1998 - reduce using PC_FACTOR

	// rotate two points about x/z axis and perform perspective projection
	GetSinCos(viewpoint_x_angle, &sin_x, &cos_x);
	GetSinCos(viewpoint_z_angle, &sin_z, &cos_z);
	for (i = 0; i < 2; i++)
		{
		x = plane[i].x;
		y = plane[i].y;
		z = plane[i].z;

		y += y_adjust;

		// rotate about x axis
		trans_y = (y * cos_x) - (z * sin_x);
		trans_z = (y * sin_x) + (z * cos_x);

		// rotate about z axis
		y = trans_y >> LOG_PRECISION;
		trans_x = (x * cos_z) - (y * sin_z);
		trans_y = (x * sin_z) + (y * cos_z);

		// perspective projection
		z = trans_z >> LOG_FOCUS;

		// debug stuff
		if (z == 0)
			{
#if defined(DEBUG) || defined(_DEBUG)
			fprintf(out, "7.  Preventing division by zero\n");
			//Sleep(10);
#endif

			z = 1;
			}

		x = (trans_x / z) + screen_width/2;
		y = (trans_y / z) + screen_height/2;

		// store screen x and screen y
		screen_coords[i].x = x;
		screen_coords[i].y = y;
		}

	x1 = screen_coords[0].x; y1 = screen_coords[0].y;
	x2 = screen_coords[1].x; y2 = screen_coords[1].y;

	//fprintf(out, "(x1,y1) = (%d,%d), (x2,y2) = (%d,%d)\n", x1, y1, x2, y2);

	// draw required rectangular sections
	int32_t min_x = 0;
	int32_t min_y = 0;
	int32_t max_x = screen_width - 1;
	int32_t max_y = screen_height - 1;
	int32_t on_screen, draw, ytop = 0, ybottom = 0, colour_index = 0;

	if ((x1 > x2) || ((x1 == x2) && (y1 > y2)))
		upside_down = (! upside_down);

	on_screen = ClipLine(&x1, &y1, &x2, &y2, screen_width, screen_height);

	//fprintf(out, "upside_down = %d, on_screen = %d\n", upside_down, on_screen);
	//fprintf(out, "clipped (x1,y1) = (%d,%d), (x2,y2) = (%d,%d)\n\n", x1, y1, x2, y2);

	// get smallest and largest y
	if (y2 < y1)
		{
		xs = x2;
		ys = y2;
		xl = x1;
		yl = y1;
		}
	else
		{
		xs = x1;
		ys = y1;
		xl = x2;
		yl = y2;
		}


	// special check for when line has been clipped to a single
	// pixel (i.e. one of the four corners of the screen)
	if ((xs == xl) && (ys == yl))
		on_screen = FALSE;


	if (on_screen)
		{
		// draw top rectangle
		draw = FALSE;

		if (! upside_down)
			{
			colour_index = SKY_COLOUR;

			if (ys != yl)
				{
				ytop = 0;
				if ((xl > min_x) && (xl < max_x))	// if not at either edge
					ybottom = yl;
				else
					ybottom = yl - 1;

				draw = TRUE;
				}
			else	// ys == yl
				if (ys > 0)
					{
					ytop = 0;
					if (((xs == min_x) && (xl == max_x)) ||
						((xs == max_x) && (xl == min_x)))	// if at both edges
						ybottom = ys - 1;
					else
						ybottom = ys;

					draw = TRUE;
					}
			}
		else	// upside down
			{
			colour_index = GROUND_COLOUR;

			if (ys != yl)
				{
				if (ys > 0)
					{
					ytop = 0;
					ybottom = ys - 1;

					draw = TRUE;
					}
				}
			else	// ys == yl
				{
				if (((xs == min_x) && (xl == max_x)) ||
					((xs == max_x) && (xl == min_x)))	// if at both edges
					{
					ytop = 0;
					ybottom = ys;

					draw = TRUE;
					}
				}
			}

		if (draw)
			{
			DrawFilledRectangle(min_x, ytop, max_x, ybottom, SCRGB(colour_index));
			}


		// draw bottom rectangle
		// simply fill the area that the above rectangle missed
		if (colour_index == SKY_COLOUR)
			colour_index = GROUND_COLOUR;
		else
			colour_index = SKY_COLOUR;

		if (draw)	// if previous rectangle was drawn
			{
			if (ybottom < max_y)
				{
				DrawFilledRectangle(min_x, ybottom+1, max_x, max_y, SCRGB(colour_index));
				}
			}
		else
			{
			DrawFilledRectangle(min_x, min_y, max_x, max_y, SCRGB(colour_index));
			}
		}
	else	// horizon line is off screen
		{
		int32_t off_top = FALSE, off_bottom = FALSE;
		int32_t off_left = FALSE, off_right = FALSE;

		if ((y1 <= min_y) && (y2 <= min_y))
			off_top = TRUE;

		if ((y1 >= max_y) && (y2 >= max_y))
			off_bottom = TRUE;

		if ((x1 <= min_x) && (x2 <= min_x))
			off_left = TRUE;

		if ((x1 >= max_x) && (x2 >= max_x))
			off_right = TRUE;

		if (off_top)
			{
			if (! upside_down)
				colour_index = GROUND_COLOUR;
			else
				colour_index = SKY_COLOUR;
			}
		else if (off_bottom)
			{
			if (! upside_down)
				colour_index = SKY_COLOUR;
			else
				colour_index = GROUND_COLOUR;
			}
		else if (off_left)
			{
			if (y1 > y2)
				colour_index = GROUND_COLOUR;
			else
				colour_index = SKY_COLOUR;
			}
		else if (off_right)
			{
			if (y1 > y2)
				colour_index = SKY_COLOUR;
			else
				colour_index = GROUND_COLOUR;
			}

		DrawFilledRectangle(min_x, min_y, max_x, max_y, SCRGB(colour_index));
		}


	// draw sloping ground section
	if ((on_screen) && (y1 != y2))
		{
		int32_t sides = 0;
		POINT points[MAX_POLY_SIDES];

		if (y1 < y2)
			{
			// ground on left area of screen

			// 17/02/1999 - add one to y2 because Direct3D doesn't include last pixel
			// (D3DRENDERSTATE_LASTPIXEL doesn't seem to have any effect)
			++y2;

			if (x1 > min_x)
				{
				points[sides].x = min_x; points[sides].y = y1; sides++;
				}

			points[sides].x = x1; points[sides].y = y1; sides++;
			points[sides].x = x2; points[sides].y = y2; sides++;

			if (x2 > min_x)
				{
				points[sides].x = min_x; points[sides].y = y2; sides++;
				}
			}
		else
			{
			// ground on right area of screen

			// 17/02/1999 - add one to y1, max_x because Direct3D doesn't include last pixel
			// (D3DRENDERSTATE_LASTPIXEL doesn't seem to have any effect)
			++y1;
			++max_x;

			if (x1 < max_x)
				{
				points[sides].x = max_x; points[sides].y = y1; sides++;
				}

			points[sides].x = x1; points[sides].y = y1; sides++;
			points[sides].x = x2; points[sides].y = y2; sides++;

			if (x2 < max_x)
				{
				points[sides].x = max_x; points[sides].y = y2; sides++;
				}
			}

		SetTextureColour(GROUND_COLOUR);
		if (sides >= 3)
			DrawPolygon(points, sides);
		}
	}

/*	======================================================================================= */
/*	Function:		DrawScenery																*/
/*																							*/
/*	Description:	Draw the current scenery using the supplied viewpoint					*/
/*	======================================================================================= */

#define	NUM_SCENERY_OBJECTS	32

#define	MAX_SCENERY_COORDS	7

#define	SCENERY_X_Y_SCALE_FACTOR	64	// (z of 0x00010000, divided by (4 * FOCUS))

typedef struct
	{
	COORD_3D *coords;
	int32_t	coordsSize;		// memory size for coords
	int32_t	numPolygons;
	int32_t	*polygons;
	} SCENERY;

/* Scenery data (moved to file scope so the 3D scenery builder can share it) */
	// scenery y positions (range 0-255)
	static int32_t scenery_positions[NUM_SCENERY_OBJECTS] =
													{0x05,0x0f,0x15,0x1f,0x25,0x2f,0x35,0x3f,
													 0x45,0x4f,0x55,0x5f,0x65,0x6f,0x75,0x7f,
													 0x85,0x8f,0x95,0x9f,0xa5,0xaf,0xb5,0xbf,
													 0xc5,0xcf,0xd5,0xdf,0xe5,0xef,0xf5,0xff};

	// scenery numbers (i.e. IDs, range 0-24)
	static int32_t standard_numbers[NUM_SCENERY_OBJECTS] =
													{0,13,10,11,12,5,2,3,
													 0,1,4,5,2,1,0,5,
													 2,3,4,5,0,9,6,7,
													 8,5,0,3,4,1,2,5};

	static int32_t taller_numbers[NUM_SCENERY_OBJECTS] =
													{14,15,15,14,15,14,14,15,
													 14,15,14,15,15,14,15,14,
													 14,15,15,14,15,15,14,14,
													 15,14,15,15,14,14,14,15};

	static int32_t snowcapped_numbers[NUM_SCENERY_OBJECTS] =
													{16,17,18,19,17,16,17,18,
													 18,16,19,17,19,18,18,16,
													 19,17,17,18,16,16,19,18,
													 17,16,19,19,17,18,16,19};

	static int32_t building_numbers[NUM_SCENERY_OBJECTS] =
													{20,21,22,23,23,21,20,20,
													 21,22,22,20,20,21,23,22,
													 21,20,21,21,23,22,20,21,
													 23,23,22,20,21,20,21,23};

	static int32_t mixed_numbers[NUM_SCENERY_OBJECTS] =
													{16,0,18,1,17,2,17,3,
													 18,4,19,5,20,24,21,24,
													 23,22,12,19,6,16,7,18,
													 8,16,9,19,10,18,11,19};

	static int32_t *scenery_types[] =
								{standard_numbers,
								 taller_numbers,
								 snowcapped_numbers,
								 building_numbers,
								 mixed_numbers};


	// scenery co-ordinates
	static COORD_3D standard1_c[] =
								{{0,0,0x00010000},	// x, y, z
								 {0x180,0,0x00010000},
								 {0x4b,0x1c,0x00010000},
								 {0x104,0x10,0x00010000}};

	static COORD_3D standard2_c[] =
								{{0,0,0x00010000},
								 {0x100,0,0x00010000},
								 {0x7d,0x12,0x00010000},
								 {0xc0,0x1e,0x00010000}};

	static COORD_3D standard3_c[] =
								{{0,0,0x00010000},
								 {0x180,0,0x00010000},
								 {0x64,0x14,0x00010000},
								 {0x136,0x25,0x00010000}};

	static COORD_3D standard4_c[] =
								{{0,0,0x00010000},
								 {0x100,0,0x00010000},
								 {0x46,0x18,0x00010000},
								 {0xd8,0x24,0x00010000}};

	static COORD_3D standard5_c[] =
								{{0,0,0x00010000},
								 {0x180,0,0x00010000},
								 {0xc8,0x27,0x00010000},
								 {0xf0,0x1f,0x00010000}};

	static COORD_3D standard6_c[] =
								{{0,0,0x00010000},
								 {0x100,0,0x00010000},
								 {0x32,0xc,0x00010000},
								 {0xa8,0x1a,0x00010000}};

	static COORD_3D standard7_c[] =
								{{0,0,0x00010000},
								 {0x172,0,0x00010000},
								 {0x70,0x19,0x00010000},
								 {0xe6,0x14,0x00010000}};

	static COORD_3D standard8_c[] =
								{{0,0,0x00010000},
								 {0xfa,0,0x00010000},
								 {0x64,0xc,0x00010000},
								 {0xbb,0x12,0x00010000}};

	static COORD_3D standard9_c[] =
								{{0,0,0x00010000},
								 {0x180,0,0x00010000},
								 {0xc6,0x1c,0x00010000},
								 {0x13b,0x18,0x00010000}};

	static COORD_3D standard10_c[] =
								{{0,0,0x00010000},
								 {0x100,0,0x00010000},
								 {0x23,0x28,0x00010000},
								 {0x6e,0x37,0x00010000}};

	static COORD_3D standard11_c[] =
								{{0,0,0x00010000},
								 {0x159,0,0x00010000},
								 {0x5c,0x2a,0x00010000},
								 {0xf0,0x1e,0x00010000}};

	static COORD_3D standard12_c[] =
								{{0,0,0x00010000},
								 {0xfa,0,0x00010000},
								 {0x2d,0xf,0x00010000},
								 {0x80,0xb,0x00010000}};

	static COORD_3D standard13_c[] =
								{{0,0,0x00010000},
								 {0x17c,0,0x00010000},
								 {0x88,0x2b,0x00010000},
								 {0xd2,0x23,0x00010000}};

	static COORD_3D standard14_c[] =
								{{0,0,0x00010000},
								 {0x100,0,0x00010000},
								 {0x4b,0x29,0x00010000},
								 {0x9b,0x37,0x00010000}};

	static COORD_3D taller1_c[] =
								{{0,0,0x00010000},
								 {0x17c,0,0x00010000},
								 {0x88,0,0x00010000},
								 {0x2b,0xd2,0x00010000}};

	static COORD_3D taller2_c[] =
								{{0,0,0x00010000},
								 {0x100,0,0x00010000},
								 {0x4b,0,0x00010000},
								 {0x29,0x9b,0x00010000}};

	static COORD_3D snowcapped1_c[] =
								{{0,0,0x00010000},
								 {0xfa,0,0x00010000},
								 {0x1a4,0,0x00010000},
								 {0x253,0,0x00010000},
								 {0x181,0x2e,0x00010000},
								 {0x118,0x34,0x00010000},
								 {0x19f,0x73,0x00010000}};

	static COORD_3D snowcapped2_c[] =
								{{0,0,0x00010000},
								 {0x4b,0,0x00010000},
								 {0x127,0,0x00010000},
								 {0x1f4,0,0x00010000},
								 {0xaf,0x32,0x00010000},
								 {0x87,0x3c,0x00010000},
								 {0xff,0x48,0x00010000}};

	static COORD_3D snowcapped3_c[] =
								{{0,0,0x00010000},
								 {0x87,0,0x00010000},
								 {0xc5,0,0x00010000},
								 {0xfa,0,0x00010000},
								 {0x96,0x46,0x00010000},
								 {0x69,0x50,0x00010000},
								 {0xaa,0x5f,0x00010000}};

	static COORD_3D snowcapped4_c[] =
								{{0,0,0x00010000},
								 {0x87,0,0x00010000},
								 {0x113,0,0x00010000},
								 {0x1a9,0,0x00010000},
								 {0x91,0x2a,0x00010000},
								 {0x3c,0x32,0x00010000},
								 {0x8c,0x4d,0x00010000}};

	static COORD_3D building1_c[] =
								{{0,0,0x00010000},
								 {0x10,0,0x00010000},
								 {0x18,0,0x00010000},
								 {0,0x50,0x00010000},
								 {0x10,0x50,0x00010000},
								 {0x18,0x50,0x00010000}};

	static COORD_3D building2_c[] =
								{{0,0,0x00010000},
								 {0x10,0,0x00010000},
								 {0x18,0,0x00010000},
								 {0,0x3c,0x00010000},
								 {0x10,0x3c,0x00010000},
								 {0x18,0x3c,0x00010000}};

	static COORD_3D building3_c[] =
								{{0,0,0x00010000},
								 {0x28,0,0x00010000},
								 {0x3c,0,0x00010000},
								 {0,0x39,0x00010000},
								 {0x28,0x39,0x00010000},
								 {0x3c,0x39,0x00010000}};

	static COORD_3D building4_c[] =
								{{0,0,0x00010000},
								 {0x69,0,0x00010000},
								 {0x7d,0,0x00010000},
								 {0,0x2a,0x00010000},
								 {0x69,0x2a,0x00010000},
								 {0x7d,0x2a,0x00010000}};

	static COORD_3D lake_c[] =
								{{0,8,0x00010000},
								 {0x32,0,0x00010000},
								 {0x28a,0x0,0x00010000},
								 {0x2bc,8,0x00010000}};

	// scenery polygons
	static int32_t standard_p[] =
							{5,				// colour
							 4,				// number of sides
							 1, 0, 2, 3};	// co-ordinate offsets

	static int32_t taller_p[] =
							{4,
							 3,
							 2, 0, 3,
							 5,
							 3,
							 1, 2, 3};

	static int32_t snowcapped_p[] =
							{4,
							 4,
							 1, 0, 5, 4,
							 5,
							 3,
							 2, 1, 4,
							 5,
							 4,
							 3, 2, 4, 6,
							 15,
							 3,
							 4, 5, 6};

	static int32_t building_p[] =
							{15,
							 4,
							 1, 0, 3, 4,
							 14,
							 4,
							 2, 1, 4, 5};

	static int32_t lake_p[] =
							{6,
							 4,
							 2, 1, 0, 3};

	// scenery definitions
	static SCENERY standard1 = {standard1_c, sizeof(standard1_c), 1, standard_p};
	static SCENERY standard2 = {standard2_c, sizeof(standard2_c), 1, standard_p};
	static SCENERY standard3 = {standard3_c, sizeof(standard3_c), 1, standard_p};
	static SCENERY standard4 = {standard4_c, sizeof(standard4_c), 1, standard_p};
	static SCENERY standard5 = {standard5_c, sizeof(standard5_c), 1, standard_p};
	static SCENERY standard6 = {standard6_c, sizeof(standard6_c), 1, standard_p};
	static SCENERY standard7 = {standard7_c, sizeof(standard7_c), 1, standard_p};
	static SCENERY standard8 = {standard8_c, sizeof(standard8_c), 1, standard_p};
	static SCENERY standard9 = {standard9_c, sizeof(standard9_c), 1, standard_p};
	static SCENERY standard10 = {standard10_c, sizeof(standard10_c), 1, standard_p};
	static SCENERY standard11 = {standard11_c, sizeof(standard11_c), 1, standard_p};
	static SCENERY standard12 = {standard12_c, sizeof(standard12_c), 1, standard_p};
	static SCENERY standard13 = {standard13_c, sizeof(standard13_c), 1, standard_p};
	static SCENERY standard14 = {standard14_c, sizeof(standard14_c), 1, standard_p};

	static SCENERY taller1 = {taller1_c, sizeof(taller1_c), 2, taller_p};
	static SCENERY taller2 = {taller2_c, sizeof(taller2_c), 2, taller_p};

	static SCENERY snowcapped1 = {snowcapped1_c, sizeof(snowcapped1_c), 4, snowcapped_p};
	static SCENERY snowcapped2 = {snowcapped2_c, sizeof(snowcapped2_c), 4, snowcapped_p};
	static SCENERY snowcapped3 = {snowcapped3_c, sizeof(snowcapped3_c), 4, snowcapped_p};
	static SCENERY snowcapped4 = {snowcapped4_c, sizeof(snowcapped4_c), 4, snowcapped_p};

	static SCENERY building1 = {building1_c, sizeof(building1_c), 2, building_p};
	static SCENERY building2 = {building2_c, sizeof(building2_c), 2, building_p};
	static SCENERY building3 = {building3_c, sizeof(building3_c), 2, building_p};
	static SCENERY building4 = {building4_c, sizeof(building4_c), 2, building_p};

	static SCENERY lake = {lake_c, sizeof(lake_c), 1, lake_p};

	static SCENERY *scenery_objects[NUM_SCENERY_OBJECTS] =
		{
		&standard1,
		&standard2,
		&standard3,
		&standard4,
		&standard5,
		&standard6,
		&standard7,
		&standard8,
		&standard9,
		&standard10,
		&standard11,
		&standard12,
		&standard13,
		&standard14,
		&taller1,
		&taller2,
		&snowcapped1,
		&snowcapped2,
		&snowcapped3,
		&snowcapped4,
		&building1,
		&building2,
		&building3,
		&building4,
		&lake
		};



static void DrawScenery( int32_t viewpoint_y,
						 int32_t viewpoint_x_angle,
						 int32_t viewpoint_y_angle,
						 int32_t viewpoint_z_angle )
	{
	int32_t *scenery_numbers = scenery_types[current_scenery_type];

	SCENERY *scenery;

	COORD_3D *scenery_coords;
	COORD_2D screen_coords[MAX_SCENERY_COORDS];

	int32_t m, i, j, number, sides, offset;
	int32_t position, y_angle, visible;
	short sin_x, sin_y, sin_z;
	short cos_x, cos_y, cos_z;
	int32_t x, y, z;
	int32_t trans_x, trans_y, trans_z;
	int32_t screen_width, screen_height;
	int32_t *polygons;

	BYTE colour;
	POINT points[MAX_POLY_SIDES];


	// start of code
	GetScreenDimensions(&screen_width, &screen_height);

	// x/z angle are fixed for all scenery objects
	GetSinCos(viewpoint_x_angle, &sin_x, &cos_x);
	GetSinCos(viewpoint_z_angle, &sin_z, &cos_z);

	for (m = 0; m < NUM_SCENERY_OBJECTS; m++)
		{
		// get pointer to scenery definition
		number = scenery_numbers[m];
		scenery = scenery_objects[number];

		// get pointer to scenery co-ordinates
		scenery_coords = scenery->coords;

		// calculate number of co-ordinates
		number = scenery->coordsSize / sizeof(COORD_3D);

		// calculate y angle for this scenery object
		position = scenery_positions[m] * 256;
		y_angle = viewpoint_y_angle + position;

		y_angle &= (MAX_ANGLE - 1);
		// 29/06/1998 - temporarily reverse y_angle
		y_angle = (-y_angle & (MAX_ANGLE - 1));

		GetSinCos(y_angle, &sin_y, &cos_y);

		// rotate scenery about x/y/z axis and perform perspective projection
		visible = TRUE;
		for (i = 0; i < number; i++)
			{
			x = scenery_coords[i].x * SCENERY_X_Y_SCALE_FACTOR;
			y = -(scenery_coords[i].y * SCENERY_X_Y_SCALE_FACTOR);
			z = scenery_coords[i].z;

			y -= ((viewpoint_y / 2) >> LOG_PRECISION);	// 24/04/1998 - reduce using PC_FACTOR

			// prevent sky showing through between scenery and ground
			y += (2 * SCENERY_X_Y_SCALE_FACTOR);

			// rotate about y axis
			trans_x = (x * cos_y) + (z * sin_y);
			trans_z = (z * cos_y) - (x * sin_y);

			// rotate about x axis
			z = trans_z >> LOG_PRECISION;
			trans_y = (y * cos_x) - (z * sin_x);
			trans_z = (y * sin_x) + (z * cos_x);

			// rotate about z axis
			x = trans_x >> LOG_PRECISION;
			y = trans_y >> LOG_PRECISION;
			trans_x = (x * cos_z) - (y * sin_z);
			trans_y = (x * sin_z) + (y * cos_z);

			// skip this scenery object if any z negative (i.e. infront of screen)
			if (trans_z <= 0)
				{
				visible = FALSE;
				break;
				}

			// could also eliminate scenery if selected points are outside the
			// viewing pyramid, although the saving would probably be negligible

			// perspective projection
			z = trans_z >> LOG_FOCUS;

			// debug stuff
			if (z == 0)
				{
#if defined(DEBUG) || defined(_DEBUG)
				fprintf(out, "8.  Preventing division by zero\n");
				//Sleep(10);
#endif

				z = 1;
				}

			x = (trans_x / z) + screen_width/2;
			y = (trans_y / z) + screen_height/2;

			// store screen x and screen y
			screen_coords[i].x = x;
			screen_coords[i].y = y;
			}

		if (! visible)
			continue;

		// draw scenery object
		polygons = scenery->polygons;
		for (i = 0; i < scenery->numPolygons; i++)
			{
			colour = (BYTE)*polygons++;

			sides = *polygons++;

			// store all polygon's points
			for (j = 0; j < sides; j++)
				{
				offset = *polygons++;
				points[j].x = screen_coords[offset].x;
				points[j].y = screen_coords[offset].y;
				}

			// draw current polygon
			SetTextureColour(SCR_BASE_COLOUR+colour);
			if (sides >= 3)
				DrawPolygon(points, sides);
			}
		}
	}

/*	======================================================================================= */
/*	Function:		ClipLine																*/
/*																							*/
/*	Description:	Clip line co-ordinates to screen boundaries								*/
/*					Return FALSE if line is entirely off screen								*/
/*	======================================================================================= */

static int32_t ClipLine( int32_t *x1ptr,
					  int32_t *y1ptr,
					  int32_t *x2ptr,
					  int32_t *y2ptr,
					  int32_t screen_width,
					  int32_t screen_height )
	{
	int32_t on_screen = FALSE;
	int32_t x1 = *x1ptr, y1 = *y1ptr,
		 x2 = *x2ptr, y2 = *y2ptr;
	int32_t max_x = screen_width - 1;
	int32_t max_y = screen_height - 1;

	// clip x1
	if (x1 < 0)
		{
		// x1 is off left of screen
		if (x2 < 0)
			goto store_line;		// entire line is off left of screen
		else
			{
			// clip line to left edge, giving a new value for y1
			y1 -= ((x1 * (y2-y1)) / (x2-x1));
			x1 = 0;
			}
		}
	else
		if (x1 > max_x)
			{
			// x1 is off right of screen
			if (x2 > max_x)
				goto store_line;		// entire line is off right of screen
			else
				{
				// clip line to right edge, giving a new value for y1
				y1 -= (((x1-max_x) * (y2-y1)) / (x2-x1));
				x1 = max_x;
				}
			}


	// clip y1
	if (y1 < 0)
		{
		// y1 is off top of screen
		if (y2 < 0)
			goto store_line;		// entire line is off top of screen
		else
			{
			// clip line to top edge, giving a new value for x1
			x1 -= ((y1 * (x2-x1)) / (y2-y1));
			y1 = 0;

			// check new x1 is on screen
			if ((x1 < 0) || (x1 > max_x))
				goto store_line;
			}
		}
	else
		if (y1 > max_y)
			{
			// y1 is off bottom of screen
			if (y2 > max_y)
				goto store_line;		// entire line is off bottom of screen
			else
				{
				// clip line to bottom edge, giving a new value for x1
				x1 -= (((y1-max_y) * (x2-x1)) / (y2-y1));
				y1 = max_y;

				// check new x1 is on screen
				if ((x1 < 0) || (x1 > max_x))
					goto store_line;
				}
			}


	// clip x2
	if (x2 < 0)
		{
		// x2 is off left of screen
		// clip line to left edge, giving a new value for y2
		y2 -= ((x2 * (y1-y2)) / (x1-x2));
		x2 = 0;
		}
	else
		if (x2 > max_x)
			{
			// x2 is off right of screen
			// clip line to right edge, giving a new value for y2
			y2 -= (((x2-max_x) * (y1-y2)) / (x1-x2));
			x2 = max_x;
			}


	// clip y2
	if (y2 < 0)
		{
		// y2 is off top of screen
		// clip line to top edge, giving a new value for x2
		x2 -= ((y2 * (x1-x2)) / (y1-y2));
		y2 = 0;

		// check new x2 is on screen
		if ((x2 < 0) || (x2 > max_x))
			goto store_line;
		}
	else
		if (y2 > max_y)
			{
			// y2 is off bottom of screen
			// clip line to bottom edge, giving a new value for x2
			x2 -= (((y2-max_y) * (x1-x2)) / (y1-y2));
			y2 = max_y;

			// check new x2 is on screen
			if ((x2 < 0) || (x2 > max_x))
				goto store_line;
			}

	on_screen = TRUE;

store_line:
	// store clipped points (possibly only partially clipped if off screen)
	*x1ptr = x1; *y1ptr = y1;
	*x2ptr = x2; *y2ptr = y2;
	return(on_screen);
	}


/*	======================================================================================= */
/*	Function:		BuildSceneryMesh														*/
/*																							*/
/*	Description:	Build the current scenery type as real 3D geometry: a ring of			*/
/*					silhouettes around the world centre at `radius` game units, with the	*/
/*					same angular size and placement as the original 2D backdrop.			*/
/*					Returns the number of vertices written (triangle list).				*/
/*	======================================================================================= */

int32_t BuildSceneryMesh( float centre_x, float centre_z, float radius, UTVERTEX *out_vertices, int32_t max_vertices )
	{
	int32_t *scenery_numbers = scenery_types[current_scenery_type];
	int32_t count = 0;
	const float k = radius / 65536.0f;		// original objects sit at z = 0x10000

	for (int32_t m = 0; m < NUM_SCENERY_OBJECTS; m++)
		{
		SCENERY *scenery = scenery_objects[scenery_numbers[m]];
		COORD_3D *c = scenery->coords;
		float angle = (float)(scenery_positions[m] * 256) * 2.0f * 3.14159265f / (float)MAX_ANGLE;
		float sa = sinf(angle), ca = cosf(angle);

		// object local -> world (rotate about y, then scale out to the ring radius)
		D3DXVECTOR3 world[MAX_SCENERY_COORDS];
		int32_t n = scenery->coordsSize / (int32_t)sizeof(COORD_3D);
		for (int32_t i = 0; i < n && i < MAX_SCENERY_COORDS; i++)
			{
			float x = (float)(c[i].x * SCENERY_X_Y_SCALE_FACTOR);
			float y = (float)(c[i].y * SCENERY_X_Y_SCALE_FACTOR) - (float)(2 * SCENERY_X_Y_SCALE_FACTOR);
			float z = (float)c[i].z;
			float wx = x * ca + z * sa;
			float wz = z * ca - x * sa;
			world[i] = D3DXVECTOR3(centre_x + wx * k, y * k, centre_z + wz * k);
			}

		int32_t *polygons = scenery->polygons;
		for (int32_t p = 0; p < scenery->numPolygons; p++)
			{
			DWORD colour = SCRGB(SCR_BASE_COLOUR + *polygons++);
			int32_t sides = *polygons++;
			int32_t idx[MAX_POLY_SIDES];
			for (int32_t j = 0; j < sides; j++) idx[j] = *polygons++;
			// triangle fan
			for (int32_t j = 1; j + 1 < sides; j++)
				{
				if (count + 3 > max_vertices) return count;
				int32_t tri[3] = { idx[0], idx[j], idx[j + 1] };
				for (int32_t t = 0; t < 3; t++)
					{
					UTVERTEX &v = out_vertices[count++];
					v.pos = world[tri[t]];
					v.color = colour;
					v.tu = v.tv = 0.0f;
					v.tex = -1.0f;
					}
				}
			}
		}
	return count;
	}

int32_t GetSceneryType( void ) { return current_scenery_type; }
