//
// Copyright (C) 1993-1996 Id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//

// R_main.c

#include <math.h>
#include "DoomDef.h"
#if (APPVER_DOOMREV < AV_DR_DM12)
extern int _wp1, _wp2, _wp3, _wp4, _wp5, _wp6, _wp7, _wp8, _wp9, _wp10;
extern int _wp11, _wp12, _wp13, _wp14, _wp15, _wp16, _wp17, _wp18, _wp19;
extern int _wp20, _wp21;
#else
extern int _wp1, _wp2, _wp3, _wp4, _wp5, _wp6, _wp7, _wp8, _wp9, _wp10;
extern int _wp11, _wp12, _wp13, _wp14, _wp15, _wp16, _wp17, _wp18, _wp19;
#endif
#include "R_local.h"
/*

*/

int			viewangleoffset;

int			validcount = 1;		// increment every time a check is made

lighttable_t	*fixedcolormap;
extern	lighttable_t	**walllights;

int				centerx, centery;
fixed_t			centerxfrac, centeryfrac;
fixed_t			projection;

int				framecount;		// just for profiling purposes

int		sscount, linecount, loopcount;

fixed_t		viewx, viewy, viewz;
angle_t		viewangle;
fixed_t		viewcos, viewsin;
player_t	*viewplayer;

int				detailshift;		// 0 = high, 1 = low

//
// precalculated math tables
//
angle_t		clipangle;

// The viewangletox[viewangle + FINEANGLES/4] lookup maps the visible view
// angles  to screen X coordinates, flattening the arc to a flat projection
// plane.  There will be many angles mapped to the same X.
int			viewangletox[FINEANGLES/2];

// The xtoviewangleangle[] table maps a screen pixel to the lowest viewangle
// that maps back to x ranges from clipangle to -clipangle
angle_t		xtoviewangle[SCREENWIDTH+1];

// the finetangentgent[angle+FINEANGLES/4] table holds the fixed_t tangent
// values for view angles, ranging from MININT to 0 to MAXINT.
#if (APPVER_DOOMREV < AV_DR_DM12)
fixed_t		finetangent[FINEANGLES/2];

fixed_t		finesine[5*FINEANGLES/4];
#endif
fixed_t		*finecosine = &finesine[FINEANGLES/4];


lighttable_t	*scalelight[LIGHTLEVELS][MAXLIGHTSCALE];
lighttable_t	*scalelightfixed[MAXLIGHTSCALE];
lighttable_t	*zlight[LIGHTLEVELS][MAXLIGHTZ];

int			extralight;			// bumped light from gun blasts

void		(*colfunc) (void);
void		(*basecolfunc) (void);
void		(*fuzzcolfunc) (void);
void		(*transcolfunc) (void);
void		(*spanfunc) (void);

/*
===================
=
= R_AddPointToBox
=
===================
*/

void R_AddPointToBox (int x, int y, fixed_t *box)
{
	if (x< box[BOXLEFT])
		box[BOXLEFT] = x;
	if (x> box[BOXRIGHT])
		box[BOXRIGHT] = x;
	if (y< box[BOXBOTTOM])
		box[BOXBOTTOM] = y;
	if (y> box[BOXTOP])
		box[BOXTOP] = y;
}



/*
===============================================================================
=
= R_PointOnSide
=
= Returns side 0 (front) or 1 (back)
===============================================================================
*/

int	R_PointOnSide (fixed_t x, fixed_t y, node_t *node)
{
	fixed_t	dx,dy;
	fixed_t	left, right;

	if (!node->dx)
	{
		if (x <= node->x)
			return node->dy > 0;
		return node->dy < 0;
	}
	if (!node->dy)
	{
		if (y <= node->y)
			return node->dx < 0;
		return node->dx > 0;
	}

	dx = (x - node->x);
	dy = (y - node->y);

// try to quickly decide by looking at sign bits
	if ( (node->dy ^ node->dx ^ dx ^ dy)&0x80000000 )
	{
		if  ( (node->dy ^ dx) & 0x80000000 )
			return 1;	// (left is negative)
		return 0;
	}

	left = FixedMul ( node->dy>>FRACBITS , dx );
	right = FixedMul ( dy , node->dx>>FRACBITS );

	if (right < left)
		return 0;		// front side
	return 1;			// back side
}


int	R_PointOnSegSide (fixed_t x, fixed_t y, seg_t *line)
{
	fixed_t	lx, ly;
	fixed_t	ldx, ldy;
	fixed_t	dx,dy;
	fixed_t	left, right;

	lx = line->v1->x;
	ly = line->v1->y;

	ldx = line->v2->x - lx;
	ldy = line->v2->y - ly;

	if (!ldx)
	{
		if (x <= lx)
			return ldy > 0;
		return ldy < 0;
	}
	if (!ldy)
	{
		if (y <= ly)
			return ldx < 0;
		return ldx > 0;
	}

	dx = (x - lx);
	dy = (y - ly);

// try to quickly decide by looking at sign bits
	if ( (ldy ^ ldx ^ dx ^ dy)&0x80000000 )
	{
		if  ( (ldy ^ dx) & 0x80000000 )
			return 1;	// (left is negative)
		return 0;
	}

	left = FixedMul ( ldy>>FRACBITS , dx );
	right = FixedMul ( dy , ldx>>FRACBITS );

	if (right < left)
		return 0;		// front side
	return 1;			// back side
}


/*
===============================================================================
=
= R_PointToAngle
=
===============================================================================
*/

// to get a global angle from cartesian coordinates, the coordinates are
// flipped until they are in the first octant of the coordinate system, then
// the y (<=x) is scaled and divided by x to get a tangent (slope) value
// which is looked up in the tantoangle[] table.  The +1 size is to handle
// the case when x==y without additional checking.
#define	SLOPERANGE	2048
#define	SLOPEBITS	11
#define	DBITS		(FRACBITS-SLOPEBITS)


#if (APPVER_DOOMREV < AV_DR_DM12)
int	tantoangle[SLOPERANGE+1];
#else
extern	int	tantoangle[SLOPERANGE+1];		// get from tables.c
#endif

// int	tantoangle[SLOPERANGE+1];

int SlopeDiv (unsigned num, unsigned den)
{
	unsigned ans;
	if (den < 512)
		return SLOPERANGE;
	ans = (num<<3)/(den>>8);
	return ans <= SLOPERANGE ? ans : SLOPERANGE;
}

angle_t R_PointToAngle (fixed_t x, fixed_t y)
{
	x -= viewx;
	y -= viewy;
	if ( (!x) && (!y) )
		return 0;
	if (x>= 0)
	{	// x >=0
		if (y>= 0)
		{	// y>= 0
			if (x>y)
				return tantoangle[ SlopeDiv(y,x)];     // octant 0
			else
				return ANG90-1-tantoangle[ SlopeDiv(x,y)];  // octant 1
		}
		else
		{	// y<0
			y = -y;
			if (x>y)
				return -tantoangle[SlopeDiv(y,x)];  // octant 8
			else
				return ANG270+tantoangle[ SlopeDiv(x,y)];  // octant 7
		}
	}
	else
	{	// x<0
		x = -x;
		if (y>= 0)
		{	// y>= 0
			if (x>y)
				return ANG180-1-tantoangle[ SlopeDiv(y,x)]; // octant 3
			else
				return ANG90+ tantoangle[ SlopeDiv(x,y)];  // octant 2
		}
		else
		{	// y<0
			y = -y;
			if (x>y)
				return ANG180+tantoangle[ SlopeDiv(y,x)];  // octant 4
			else
				return ANG270-1-tantoangle[ SlopeDiv(x,y)];  // octant 5
		}
	}

	return 0;
}


angle_t R_PointToAngle2 (fixed_t x1, fixed_t y1, fixed_t x2, fixed_t y2)
{
	viewx = x1;
	viewy = y1;
	return R_PointToAngle (x2, y2);
}


fixed_t	R_PointToDist (fixed_t x, fixed_t y)
{
	int		angle;
	fixed_t	dx, dy, temp;
	fixed_t	dist;

	dx = abs(x - viewx);
	dy = abs(y - viewy);

	if (dy>dx)
	{
		temp = dx;
		dx = dy;
		dy = temp;
	}

	angle = (tantoangle[ FixedDiv(dy,dx)>>DBITS ]+ANG90) >> ANGLETOFINESHIFT;

	dist = FixedDiv (dx, finesine[angle] );	// use as cosine

	return dist;
}



/*
=================
=
= R_InitPointToAngle
=
=================
*/

void R_InitPointToAngle (void)
{
#if (APPVER_DOOMREV < AV_DR_DM12)
	int	i;
	long	t;
	float	f;
//
// slope (tangent) to angle lookup
//
	for (i=0 ; i<=SLOPERANGE ; i++)
	{
		f = atan( (float)i/SLOPERANGE )/(3.141592657*2);
		t = 0xffffffff*f;
		tantoangle[i] = t;
	}
#endif
}

//=============================================================================

/*
================
=
= R_ScaleFromGlobalAngle
=
= Returns the texture mapping scale for the current line at the given angle
= rw_distance must be calculated first
================
*/

fixed_t R_ScaleFromGlobalAngle (angle_t visangle)
{
	fixed_t		scale;
	int			anglea, angleb;
	int			sinea, sineb;
	fixed_t		num,den;

#if 0
{
	fixed_t		dist,z;
	fixed_t		sinv, cosv;

	sinv = finesine[(visangle-rw_normalangle)>>ANGLETOFINESHIFT];
	dist = FixedDiv (rw_distance, sinv);
	cosv = finecosine[(viewangle-visangle)>>ANGLETOFINESHIFT];
	z = abs(FixedMul (dist, cosv));
	scale = FixedDiv(projection, z);
	return scale;
}
#endif

	anglea = ANG90 + (visangle-viewangle);
	angleb = ANG90 + (visangle-rw_normalangle);
// bothe sines are allways positive
	sinea = finesine[anglea>>ANGLETOFINESHIFT];
	sineb = finesine[angleb>>ANGLETOFINESHIFT];
	num = FixedMul(projection,sineb)<<detailshift;
	den = FixedMul(rw_distance,sinea);
	if (den > num>>16)
	{
		scale = FixedDiv (num, den);
		if (scale > 64*FRACUNIT)
			scale = 64*FRACUNIT;
		else if (scale < 256)
			scale = 256;
	}
	else
		scale = 64*FRACUNIT;

	return scale;
}



/*
=================
=
= R_InitTables
=
=================
*/

void R_InitTables (void)
{
#if (APPVER_DOOMREV < AV_DR_DM12)
	int		i;
	float		a, fv;
	int			t;

//
// viewangle tangent table
//
	for (i=0 ; i<FINEANGLES/2 ; i++)
	{
		a = (i-FINEANGLES/4+0.5)*PI*2/FINEANGLES;
		fv = FRACUNIT*tan (a);
		t = fv;
		finetangent[i] = t;
	}

//
// finesine table
//
	for (i=0 ; i<5*FINEANGLES/4 ; i++)
	{
// OPTIMIZE: mirror...
		a = (i+0.5)*PI*2/FINEANGLES;
		t = FRACUNIT*sin (a);
		finesine[i] = t;
	}
#endif

}


/*
=================
=
= R_InitTextureMapping
=
=================
*/

void R_InitTextureMapping (void)
{
	int			i;
	int			x;
	int			t;
	fixed_t		focallength;


//
// use tangent table to generate viewangletox
// viewangletox will give the next greatest x after the view angle
//
	// calc focallength so FIELDOFVIEW angles covers SCREENWIDTH
	focallength = FixedDiv (centerxfrac
	, finetangent[FINEANGLES/4+FIELDOFVIEW/2] );

	for (i=0 ; i<FINEANGLES/2 ; i++)
	{
		if (finetangent[i] > FRACUNIT*2)
			t = -1;
		else if (finetangent[i] < -FRACUNIT*2)
			t = viewwidth+1;
		else
		{
			t = FixedMul (finetangent[i], focallength);
			t = (centerxfrac - t+FRACUNIT-1)>>FRACBITS;
			if (t < -1)
				t = -1;
			else if (t>viewwidth+1)
				t = viewwidth+1;
		}
		viewangletox[i] = t;
	}

//
// scan viewangletox[] to generate xtoviewangleangle[]
//
// xtoviewangle will give the smallest view angle that maps to x
	for (x=0;x<=viewwidth;x++)
	{
		i = 0;
		while (viewangletox[i]>x)
			i++;
		xtoviewangle[x] = (i<<ANGLETOFINESHIFT)-ANG90;
	}

//
// take out the fencepost cases from viewangletox
//
	for (i=0 ; i<FINEANGLES/2 ; i++)
	{
		t = FixedMul (finetangent[i], focallength);
		t = centerx - t;
		if (viewangletox[i] == -1)
			viewangletox[i] = 0;
		else if (viewangletox[i] == viewwidth+1)
			viewangletox[i]  = viewwidth;
	}

	clipangle = xtoviewangle[0];
}

//=============================================================================

/*
====================
=
= R_InitLightTables
=
= Only inits the zlight table, because the scalelight table changes
= with view size
=
====================
*/

#define		DISTMAP	2

void R_InitLightTables (void)
{
	int		i,j, level, startmap;
	int		scale;

//
// Calculate the light levels to use for each level / distance combination
//
	for (i=0 ; i< LIGHTLEVELS ; i++)
	{
		startmap = ((LIGHTLEVELS-1-i)*2)*NUMCOLORMAPS/LIGHTLEVELS;
		for (j=0 ; j<MAXLIGHTZ ; j++)
		{
			scale = FixedDiv ((SCREENWIDTH/2*FRACUNIT), (j+1)<<LIGHTZSHIFT);
			scale >>= LIGHTSCALESHIFT;
			level = startmap - scale/DISTMAP;
			if (level < 0)
				level = 0;
			if (level >= NUMCOLORMAPS)
				level = NUMCOLORMAPS-1;
			zlight[i][j] = colormaps + level*256;
		}
	}
}


/*
==============
=
= R_SetViewSize
=
= Don't really change anything here, because i might be in the middle of
= a refresh.  The change will take effect next refresh.
=
==============
*/

boolean	setsizeneeded;
int		setblocks, setdetail;

void R_SetViewSize (int blocks, int detail)
{
	setsizeneeded = true;
	setblocks = blocks;
	setdetail = detail;
}

/*
==============
=
= R_ExecuteSetViewSize
=
==============
*/

void R_ExecuteSetViewSize (void)
{
	fixed_t	cosadj, dy;
	int		i,j, level, startmap;

	setsizeneeded = false;

	if (setblocks == 11)
	{
		scaledviewwidth = SCREENWIDTH;
		viewheight = SCREENHEIGHT;
	}
	else
	{
		scaledviewwidth = setblocks*32;
		viewheight = (setblocks*168/10)&~7;
	}

	detailshift = setdetail;
	viewwidth = scaledviewwidth>>detailshift;

	centery = viewheight/2;
	centerx = viewwidth/2;
	centerxfrac = centerx<<FRACBITS;
	centeryfrac = centery<<FRACBITS;
	projection = centerxfrac;

	if (!detailshift)
	{
		colfunc = basecolfunc = R_DrawColumn;
		fuzzcolfunc = R_DrawFuzzColumn;
		transcolfunc = R_DrawTranslatedColumn;
		spanfunc = R_DrawSpan;
	}
	else
	{
		colfunc = basecolfunc = R_DrawColumnLow;
		fuzzcolfunc = R_DrawFuzzColumn;
		transcolfunc = R_DrawTranslatedColumn;
		spanfunc = R_DrawSpanLow;
	}

	R_InitBuffer (scaledviewwidth, viewheight);

	R_InitTextureMapping ();

//
// psprite scales
//
	pspritescale = FRACUNIT*viewwidth/SCREENWIDTH;
	pspriteiscale = FRACUNIT*SCREENWIDTH/viewwidth;

//
// thing clipping
//
	for (i=0 ; i<viewwidth ; i++)
		screenheightarray[i] = viewheight;

//
// planes
//
	for (i=0 ; i<viewheight ; i++)
	{
		dy = ((i-viewheight/2)<<FRACBITS)+FRACUNIT/2;
		dy = abs(dy);
		yslope[i] = FixedDiv ( (viewwidth<<detailshift)/2*FRACUNIT, dy);
	}

	for (i=0 ; i<viewwidth ; i++)
	{
		cosadj = abs(finecosine[xtoviewangle[i]>>ANGLETOFINESHIFT]);
		distscale[i] = FixedDiv (FRACUNIT,cosadj);
	}

//
// Calculate the light levels to use for each level / scale combination
//
	for (i=0 ; i< LIGHTLEVELS ; i++)
	{
		startmap = ((LIGHTLEVELS-1-i)*2)*NUMCOLORMAPS/LIGHTLEVELS;
		for (j=0 ; j<MAXLIGHTSCALE ; j++)
		{
			level = startmap - j*SCREENWIDTH/(viewwidth<<detailshift)/DISTMAP;
			if (level < 0)
				level = 0;
			if (level >= NUMCOLORMAPS)
				level = NUMCOLORMAPS-1;
			scalelight[i][j] = colormaps + level*256;
		}
	}
}


/*
==============
=
= R_Init
=
==============
*/

extern int	detailLevel;
extern int	screenblocks;

void R_Init (void)
{
	R_InitData ();
	printf (".");
	R_InitPointToAngle ();
	printf (".");
	R_InitTables ();
	// viewwidth / viewheight / detailLevel are set by the defaults
	printf (".");
	R_SetViewSize (screenblocks, detailLevel);
	R_InitPlanes ();
	printf (".");
	R_InitLightTables ();
	printf (".");
	R_InitSkyMap ();
	printf (".");
	R_InitTranslationTables();
	framecount = 0;
}


/*
==============
=
= R_PointInSubsector
=
==============
*/

subsector_t *R_PointInSubsector (fixed_t x, fixed_t y)
{
	node_t	*node;
	int		side, nodenum;

	if (!numnodes)				// single subsector is a special case
		return subsectors;

	nodenum = numnodes-1;

	while (! (nodenum & NF_SUBSECTOR) )
	{
		node = &nodes[nodenum];
		side = R_PointOnSide (x, y, node);
		nodenum = node->children[side];
	}

	return &subsectors[nodenum & ~NF_SUBSECTOR];

}

/*
==============
=
= R_SetupFrame
=
==============
*/

void R_SetupFrame (player_t *player)
{
	int	i;

	//drawbsp = 1;
	viewplayer = player;
	viewx = player->mo->x;
	viewy = player->mo->y;
	viewangle = player->mo->angle+viewangleoffset;
	extralight = player->extralight;
	viewz = player->viewz;
	viewsin = finesine[viewangle>>ANGLETOFINESHIFT];
	viewcos = finecosine[viewangle>>ANGLETOFINESHIFT];
	sscount = 0;
	if (player->fixedcolormap)
	{
		fixedcolormap = colormaps+player->fixedcolormap
			*256*sizeof(lighttable_t);
		walllights = scalelightfixed;
		for (i=0 ; i<MAXLIGHTSCALE ; i++)
			scalelightfixed[i] = fixedcolormap;
	}
	else
		fixedcolormap = 0;
	framecount++;
	validcount++;

#ifdef __NeXT__
	RD_ClearMapWindow ();
#endif
#ifdef __WATCOMC__
	destview = destscreen+(viewwindowx>>2)+viewwindowy*80;
#endif
}

/*
==============
=
= R_RenderView
=
==============
*/

void R_RenderPlayerView (player_t *player)
{
	R_SetupFrame (player);
	R_ClearClipSegs ();
	R_ClearDrawSegs ();
	R_ClearPlanes ();
	R_ClearSprites ();
#if (APPVER_DOOMREV >= AV_DR_DM12)
	NetUpdate ();					// check for new console commands
#endif
	R_RenderBSPNode (numnodes-1);	// the head node is the last node output
	NetUpdate ();					// check for new console commands
	R_DrawPlanes ();
#if (APPVER_DOOMREV >= AV_DR_DM12)
	NetUpdate ();					// check for new console commands
#endif
	R_DrawMasked ();
#if (APPVER_DOOMREV >= AV_DR_DM12)
	NetUpdate ();					// check for new console commands
#endif
}
