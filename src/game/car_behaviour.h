
#ifndef	_CAR_BEHAVIOUR
#define	_CAR_BEHAVIOUR

#include "scr_compat.h"

/*	========= */
/*	Constants */
/*	========= */
#define	CAR_WIDTH	64
#define	CAR_LENGTH	128

// new controls for Car Behaviour, Player 1
// must not clash with other KEY definitions
#define KEY_P1_LEFT		0x00000001l
#define KEY_P1_RIGHT	0x00000002l
#define KEY_P1_HASH		0x00000004l
#define KEY_P1_BRAKE_BOOST	0x00000008l
#define KEY_P1_ACCEL_BOOST	0x00000010l

#define	AMIGA_PAL_HZ	(3546895)

#define	REDUCTION	238		// (238/256)
#define	INCREASE	276		// (276/256)

typedef enum
	{
	OPPONENT = 0,
	PLAYER,
	NUM_CARS
	} CarType;

/*	===================== */
/*	Structure definitions */
/*	===================== */

/*	============================== */
/*	External function declarations */
/*	============================== */
// Analog driver input from a gamepad, force feedback wheel or VR controllers.
// Used when no keyboard control is pressed. steer: -1 (full left) .. +1 (full right).
typedef struct
	{
	bool  active;
	float steer;
	float throttle;		// 0..1
	float brake;		// 0..1
	bool  boost;
	float steer_deadzone;	// 0..1
	} ScrAnalogInput;

extern void SetAnalogDriverInput (const ScrAnalogInput *in);

// Physics telemetry for force feedback, haptics and HUD (see GetCarTelemetry).
// Raw values are in the original fixed-point units.
typedef struct
	{
	int32_t z_speed;			// forward speed (player_z_speed, ~120*256 is max powered speed)
	int32_t display_speed;		// speedometer value shown in the original HUD
	int32_t world_y_speed;		// vertical speed
	bool    touching_road;
	bool    on_chains;			// being lowered onto the track at start / after a crash
	bool    wrecked;
	bool    off_road;
	int32_t steering;			// left_right_value: -15 left, 0, +15 right
	int32_t target_yaw_rate;	// steering target (y_angle_difference)
	int32_t yaw_rate;			// player_y_rotation_speed
	int32_t yaw_acceleration;	// player_y_rotation_acceleration
	int32_t pitch_rate;
	int32_t roll_rate;
	int32_t damage;				// new_damage (accumulated)
	int32_t engine_revs;
	bool    boost_active;
	float   landing_impact;		// 0..1 pulse when wheels hit the road hard
	bool    car_collision;		// pulse when the cars collide
	int32_t current_piece;
	} ScrCarTelemetry;

extern void GetCarTelemetry (ScrCarTelemetry *t);

extern void ResetPlayer (void);

extern void CarBehaviour (DWORD input,
						  int32_t *x,
						  int32_t *y,
						  int32_t *z,
						  int32_t *x_angle,
						  int32_t *y_angle,
						  int32_t *z_angle);

extern void LimitViewpointY (int32_t *y);

extern int32_t AmigaVolumeToDirectX (int32_t amiga_volume);

extern int32_t CalculateDisplaySpeed (void);

extern void FramesWheelsEngine (ScrSound *engineSoundBuffers[]);
extern void EngineSoundStopped (void);

extern void CalculatePlayersRoadPosition (void);

extern void DrawOtherGraphics (void);
extern void UpdateDamage (void);

extern void ResetLapData (int32_t car);
extern void UpdateLapData (void);

#ifdef USE_AMIGA_RECORDING
extern void RequestGameReplay (void);
extern void RequestStoredReplay (void);

// Following only used for testing against Amiga
extern bool GetRecordedAmigaWord( int32_t *value_out );
extern bool GetRecordedAmigaLong( int32_t *value_out );
extern void CompareAmigaWord( char *name, int32_t amiga_value, int32_t *value );
extern void CompareRecordedAmigaWord( char *name, int32_t *value );
extern void CloseAmigaRecording( void );
#endif

#endif	/* _CAR_BEHAVIOUR */
