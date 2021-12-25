#include "DoomDef.h"
#include "soundst.h"

static channel_t *channels; // the set of channels available
static int snd_SfxVolume, snd_MusicVolume;
static boolean mus_paused;	// whether songs are mus_paused
static musicinfo_t *mus_playing=0;// music currently being played
int numChannels; // number of channels available
static int nextcleanup;

//
// Internals.
//
int S_getChannel (void *origin, sfxinfo_t *sfxinfo);
int S_AdjustSoundParams ( mobj_t *listener, mobj_t *source, int *vol, int *sep, int *pitch);
void S_StopChannel(int cnum);


void S_SetMusicVolume(int volume)
{
  if (volume < 0 || volume > 127)
    I_Error("Attempt to set music volume at %d", volume);

  I_SetMusicVolume(127);
  I_SetMusicVolume(volume);
  snd_MusicVolume = volume;
}


void S_StopMusic(void)
{
  if (mus_playing)
  {
    if (mus_paused)
      I_ResumeSong(mus_playing->handle);
    I_StopSong(mus_playing->handle);
    I_UnRegisterSong(mus_playing->handle);
    Z_ChangeTag(mus_playing->data, PU_CACHE);
#ifdef __WATCOMC__
    _dpmi_unlockregion(mus_playing->data, lumpinfo[mus_playing->lumpnum].size);
#endif
    mus_playing->data = 0;
    mus_playing = 0;
  }
}

void S_ChangeMusic (int musicnum, int looping)
{
  musicinfo_t *music;
  char namebuf[9];

  if ( (musicnum <= mus_None) || (musicnum >= NUMMUSIC) )
    I_Error("Bad music number %d", musicnum);
  else
    music = &S_music[musicnum];
  if (mus_playing == music)
    return;
  // shutdown old music
  S_StopMusic();
  // get lumpnum if neccessary
  if (!music->lumpnum)
  {
    sprintf(namebuf, "d_%s", music->name);
    music->lumpnum = W_GetNumForName(namebuf);
  }
  // load & register it
  music->data = (void *) W_CacheLumpNum(music->lumpnum, PU_MUSIC);
#ifdef __WATCOMC__
    _dpmi_lockregion(music->data, lumpinfo[music->lumpnum].size);
#endif
  music->handle = I_RegisterSong(music->data);
  // play it
  I_PlaySong(music->handle, looping);
  mus_playing = music;
}

void S_StartMusic(int m_id)
{
    S_ChangeMusic(m_id, false);
}

void S_StopChannel(int cnum)
{
  int i;
  channel_t *c = &channels[cnum];

  if (c->sfxinfo)
  {
    // stop the sound playing
    if (I_SoundIsPlaying(c->handle))
    {
#ifdef SAWDEBUG
      if (c->sfxinfo == &S_sfx[sfx_sawful])
	fprintf(stderr, "stopped\n");
#endif
      I_StopSound(c->handle);
    }
    // check to see
    //  if other channels are playing the sound
    for (i=0 ; i<numChannels ; i++)
      if (cnum != i && c->sfxinfo == channels[i].sfxinfo)
	break;
    // degrade usefulness of sound data
    c->sfxinfo->usefulness--;
    c->sfxinfo = 0;
  }
}

//
// Changes volume, stereo-separation, and pitch variables
//  from the norm of a sound effect to be played.
// If the sound is not audible, returns a 0.
// Otherwise, modifies parameters and returns 1.
//
int S_AdjustSoundParams (mobj_t *listener, mobj_t *source, int *vol, int *sep, int *pitch)
{
  fixed_t approx_dist, adx, ady;
  angle_t angle;

  // calculate the distance to sound origin and clip it if necessary
  adx = abs(listener->x - source->x);
  ady = abs(listener->y - source->y);
  // From _GG1_ p.428. Appox. eucledian distance fast.
  approx_dist = adx + ady - ((adx < ady ? adx : ady)>>1);
  if (gamemap != 8 && approx_dist > S_CLIPPING_DIST)
    return 0;
  // angle of source to listener
  angle = R_PointToAngle2(listener->x, listener->y, source->x, source->y);
  if (angle > listener->angle)
    angle = angle - listener->angle;
  else
    angle = angle + (0xffffffff - listener->angle);
  angle >>= ANGLETOFINESHIFT;
  // stereo separation
  *sep = 128 - (FixedMul(S_STEREO_SWING,finesine[angle])>>FRACBITS);
  // volume calculation
  if (approx_dist < S_CLOSE_DIST)
    *vol = snd_SfxVolume;
  else if (gamemap == 8)
  {
    if (approx_dist > S_CLIPPING_DIST)
      approx_dist = S_CLIPPING_DIST;
    *vol = 15+ ((snd_SfxVolume-15)*((S_CLIPPING_DIST - approx_dist)>>FRACBITS))
      / S_ATTENUATOR;
  }
  else
  {
    // distance effect
    *vol = (snd_SfxVolume * ((S_CLIPPING_DIST - approx_dist)>>FRACBITS))
      / S_ATTENUATOR; 
  }
  return (*vol > 0);
}

void S_SetSfxVolume(int volume)
{
  if (volume < 0 || volume > 127)
    I_Error("Attempt to set sfx volume at %d", volume);
  snd_SfxVolume = volume;
}

//
// Stop and resume music, during game PAUSE.
//
void S_PauseSound(void)
{
  if (mus_playing && !mus_paused)
  {
    I_PauseSong(mus_playing->handle);
    mus_paused = true;
  }
}

void S_ResumeSound(void)
{
  if (mus_playing && mus_paused)
  {
    I_ResumeSong(mus_playing->handle);
    mus_paused = false;
  }
}

void S_StopSound(void *origin)
{
  int cnum;

  for (cnum=0 ; cnum<numChannels ; cnum++)
  {
    if (channels[cnum].sfxinfo && channels[cnum].origin == origin)
    {
      S_StopChannel(cnum);
      break;
    }
  }
}

//
// S_getChannel :
//   If none available, return -1.  Otherwise channel #.
//
int S_getChannel (void *origin, sfxinfo_t *sfxinfo)
{
  int cnum;// channel number to use
  channel_t *c;

  // Find an open channel
  for (cnum=0 ; cnum<numChannels ; cnum++)
  {
    if (!channels[cnum].sfxinfo)
      break;
    else if (origin &&  channels[cnum].origin ==  origin)
    {
      S_StopChannel(cnum);
      break;
    }
  }
  // None available
  if (cnum == numChannels)
  {
    // Look for lower priority
    for (cnum=0 ; cnum<numChannels ; cnum++)
      if (channels[cnum].sfxinfo->priority >= sfxinfo->priority) break;
	if (cnum == numChannels)
	  return -1; // FUCK!  No lower priority.  Sorry, Charlie.    
	else
	  S_StopChannel(cnum);  // Otherwise, kick out lower priority.
  }
  c = &channels[cnum];
  // channel is decided to be cnum.
  c->sfxinfo = sfxinfo;
  c->origin = origin;
  return cnum;
}

void S_StartSoundAtVolume(void *origin_p, int sfx_id, int volume)
{
  int rc, sep, pitch, priority;
  sfxinfo_t *sfx;
  int cnum;
  mobj_t *origin = (mobj_t *) origin_p;

  // Debug.
  /*fprintf( stderr,
  	   "S_StartSoundAtVolume: playing sound %d (%s)\n",
  	   sfx_id, S_sfx[sfx_id].name );*/
  // check for bogus sound #
  if (sfx_id < 1 || sfx_id > NUMSFX)
    I_Error("Bad sfx #: %d", sfx_id);
  
  sfx = &S_sfx[sfx_id];
  
  // Initialize sound parameters
  if (sfx->link)
  {
    pitch = sfx->pitch;
    priority = sfx->priority;
    volume += sfx->volume;
    
    if (volume < 1)
      return;
    
    if (volume > snd_SfxVolume)
      volume = snd_SfxVolume;
  }	
  else
  {
    pitch = NORM_PITCH;
    priority = NORM_PRIORITY;
  }
  // Check to see if it is audible,
  //  and if not, modify the params
  if (origin && origin != players[consoleplayer].mo)
  {
    rc = S_AdjustSoundParams(players[consoleplayer].mo, origin,
			     &volume, &sep, &pitch);
    if ( origin->x == players[consoleplayer].mo->x
	 && origin->y == players[consoleplayer].mo->y)
    {	
      sep 	= NORM_SEP;
    }

    if (!rc)
      return;
  }	
  else
  {
    sep = NORM_SEP;
  }
  
  // hacks to vary the sfx pitches
  if (sfx_id >= sfx_sawup && sfx_id <= sfx_sawhit)
  {	
    pitch += 8 - (M_Random()&15);
    if (pitch<0)
      pitch = 0;
    else if (pitch>255)
      pitch = 255;
  }
  else if (sfx_id != sfx_itemup && sfx_id != sfx_tink)
  {
    pitch += 16 - (M_Random()&31);
    if (pitch<0)
      pitch = 0;
    else if (pitch>255)
      pitch = 255;
  }
  // kill old sound
  S_StopSound(origin);
  // try to find a channel
  cnum = S_getChannel(origin, sfx);  
  if (cnum<0)
    return;

  // get lumpnum if necessary
  if (sfx->lumpnum < 0)
    sfx->lumpnum = I_GetSfxLumpNum(sfx);

#ifndef SNDSRV
  // cache data if necessary
  if (!sfx->data)
  {
#ifndef __WATCOMC__
    fprintf( stderr,
	     "S_StartSoundAtVolume: 16bit and not pre-cached - wtf?\n");
#else
    sfx->data = (void *) W_CacheLumpNum(sfx->lumpnum, PU_MUSIC);
    _dpmi_lockregion(sfx->data, lumpinfo[sfx->lumpnum].size);
    // fprintf( stderr,
    //	     "S_StartSoundAtVolume: loading %d (lump %d) : 0x%x\n",
    //       sfx_id, sfx->lumpnum, (int)sfx->data );
#endif
  }
#endif
  
  // increase the usefulness
  if (sfx->usefulness++ < 0)
    sfx->usefulness = 1;
  
  // Assigns the handle to one of the channels in the
  //  mix/output buffer.
  channels[cnum].handle = I_StartSound(sfx_id, sfx->data, volume,
    sep, pitch, priority);
}

void S_StartSound (void *origin, int sfx_id)
{
#ifdef SAWDEBUG
// if (sfx_id == sfx_sawful)
// sfx_id = sfx_itemup;
#endif
  S_StartSoundAtVolume(origin, sfx_id, snd_SfxVolume);
#ifdef SAWDEBUG
{
  int i, n;
  static mobj_t *last_saw_origins[10] = {1,1,1,1,1,1,1,1,1,1};
  static int first_saw=0;
  static int next_saw=0;
	
  if (sfx_id == sfx_sawidl || sfx_id == sfx_sawful || sfx_id == sfx_sawhit)
  {
    for (i=first_saw;i!=next_saw;i=(i+1)%10)
      if (last_saw_origins[i] != origin)
	fprintf(stderr, "old origin 0x%lx != origin 0x%lx for sfx %d\n",
	  last_saw_origins[i], origin, sfx_id);    
    last_saw_origins[next_saw] = origin;
    next_saw = (next_saw + 1) % 10;
    if (next_saw == first_saw)
      first_saw = (first_saw + 1) % 10;
    for (n=i=0; i<numChannels ; i++)
    {
      if (channels[i].sfxinfo == &S_sfx[sfx_sawidl]
	|| channels[i].sfxinfo == &S_sfx[sfx_sawful]
	|| channels[i].sfxinfo == &S_sfx[sfx_sawhit]) n++;
    }
    if (n>1)
    {
      for (i=0; i<numChannels ; i++)
      {
	if (channels[i].sfxinfo == &S_sfx[sfx_sawidl]
	  || channels[i].sfxinfo == &S_sfx[sfx_sawful]
	  || channels[i].sfxinfo == &S_sfx[sfx_sawhit])
	{
	  fprintf(stderr, "chn: sfxinfo=0x%lx, origin=0x%lx, handle=%d\n",
	    channels[i].sfxinfo, channels[i].origin, channels[i].handle);
	}
      }
      fprintf(stderr, "\n");
    }
  }
}
#endif
}

//
// Updates music & sounds
//
void S_UpdateSounds(void* listener_p)
{
  int audible, cnum,volume, sep, pitch, i;
  sfxinfo_t *sfx;
  channel_t *c; 
  mobj_t *listener = (mobj_t*)listener_p;

#ifdef __WATCOMC__
  if (gametic > nextcleanup)
  {
    for (i=1 ; i<NUMSFX ; i++)
    {
      if (S_sfx[i].usefulness < 1 && S_sfx[i].usefulness > -1)
      {
        if (--S_sfx[i].usefulness == -1)
        {
          Z_ChangeTag(S_sfx[i].data, PU_CACHE);
	  _dpmi_unlockregion(S_sfx[i].data, lumpinfo[S_sfx[i].lumpnum].size);
          S_sfx[i].data = 0;
        }
      }
    }
    nextcleanup = gametic + 15;
  }
#endif
  for (cnum=0 ; cnum<numChannels ; cnum++)
  {
    c = &channels[cnum];
    sfx = c->sfxinfo;
    if (c->sfxinfo)
    {
      if (I_SoundIsPlaying(c->handle))
      {
	// initialize parameters
	volume = snd_SfxVolume;
	pitch = NORM_PITCH;
	sep = NORM_SEP;
	if (sfx->link)
	{
	  pitch = sfx->pitch;
	  volume += sfx->volume;
	  if (volume < 1)
	  {
	    S_StopChannel(cnum);
	    continue;
	  }
	  else if (volume > snd_SfxVolume)
	  {
	    volume = snd_SfxVolume;
	  }
	}
	// check non-local sounds for distance clipping
	//  or modify their params
	if (c->origin && listener_p != c->origin)
	{
	  audible = S_AdjustSoundParams(listener, c->origin,
	    &volume, &sep, &pitch);
	  if (!audible)
	  {
	    S_StopChannel(cnum);
	  }
	  else
	    I_UpdateSoundParams(c->handle, volume, sep, pitch);
	}
      }
      else
      {
	// if channel is allocated but sound has stopped,
	//  free it
	S_StopChannel(cnum);
      }
}
}
// kill music if it is a single-play && finished
// if (	mus_playing
//      && !I_QrySongPlaying(mus_playing->handle)
//      && !mus_paused )
// S_StopMusic();
}

//
// Initializes sound stuff, including volume
// Sets channels, SFX and music volume,
//  allocates channel buffer, sets S_sfx lookup.
//
void S_Init (int sfxVolume, int musicVolume)
{  
  int i;

  fprintf( stderr, "S_Init: default sfx volume %d\n", sfxVolume);

  // Whatever these did with DMX, these are rather dummies now.
  I_SetChannels(numChannels);
  
  S_SetSfxVolume(sfxVolume);
  // No music with Linux - another dummy.
  S_SetMusicVolume(musicVolume);

  // Allocating the internal channels for mixing
  // (the maximum numer of sounds rendered
  // simultaneously) within zone memory.
  channels =
    (channel_t *) Z_Malloc(numChannels*sizeof(channel_t), PU_STATIC, 0);
  
  // Free all channels for use
  for (i=0 ; i<numChannels ; i++)
    channels[i].sfxinfo = 0;
  
  // no sounds are playing, and they are not mus_paused
  mus_paused = 0;

  // Note that sounds have not been cached (yet).
  for (i=1 ; i<NUMSFX ; i++)
    S_sfx[i].lumpnum = S_sfx[i].usefulness = -1;
}

//
// Per level startup code.
// Kills playing sounds at start of level,
//  determines music if any, changes music.
//
void S_Start(void)
{
  int cnum, mnum;

  // kill all playing sounds at start of level
  //  (trust me - a good idea)
  for (cnum=0 ; cnum<numChannels ; cnum++)
    if (channels[cnum].sfxinfo)
      S_StopChannel(cnum);
  
  // start new music for the level
  mus_paused = 0;
  
  if (commercial)
    mnum = mus_runnin + gamemap - 1;
  else
  {
    int spmus[]=
    {
      // Song - Who? - Where?
      
      mus_e3m4,	// American	e4m1
      mus_e3m2,	// Romero	e4m2
      mus_e3m3,	// Shawn	e4m3
      mus_e1m5,	// American	e4m4
      mus_e2m7,	// Tim 	e4m5
      mus_e2m4,	// Romero	e4m6
      mus_e2m6,	// J.Anderson	e4m7 CHIRON.WAD
      mus_e2m5,	// Shawn	e4m8
      mus_e1m9	// Tim		e4m9
    };
    
    if (gameepisode < 4)
      mnum = mus_e1m1 + (gameepisode-1)*9 + gamemap-1;
    else
      mnum = spmus[gamemap-1];
    }	
  
  // HACK FOR COMMERCIAL
  //  if (commercial && mnum > mus_e3m9)	
  //      mnum -= mus_e3m9;
  
  S_ChangeMusic(mnum, true);
  
  nextcleanup = 15;
}

