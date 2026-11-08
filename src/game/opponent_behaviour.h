
#ifndef	_OPPONENT_BEHAVIOUR
#define	_OPPONENT_BEHAVIOUR

/*	========= */
/*	Constants */
/*	========= */
#define	NO_OPPONENT	(-1)

/*	===================== */
/*	Structure definitions */
/*	===================== */

/*	============================== */
/*	External function declarations */
/*	============================== */
extern void OpponentBehaviour (int32_t *x,
							   int32_t *y,
							   int32_t *z,
							   float *x_angle,
							   float *y_angle,
							   float *z_angle,
							   bool bOpponentPaused);

extern void CarToCarCollision( void );

extern int32_t CalculateIfWinning( int32_t start_finish_piece );

extern int32_t CalculateOpponentsDistance (void);

#endif	/* _OPPONENT_BEHAVIOUR */
