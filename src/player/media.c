//#define LOG_NDEBUG 0
#define LOG_TAG "play"
#include "media.h"
#include <plat_log.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <assert.h>
#include <time.h>

#include "gogglemsg.h"
#include "vdec2vo.h"
#include "awdmx.h"
#include "version.h"


#define LOCKFILE "/tmp/play.pid"
#define LOCKMODE (S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH)
#define PLAY_statFILE       "/tmp/play.dat"

#ifdef EVB_DEBUG
#define PLAY_HDZERO         0
#else
#define PLAY_HDZERO         1
#endif

#if(PLAY_HDZERO)
#define VO_intfTYPE         VO_INTF_BT1120
#define VO_intfSYNC         VO_OUTPUT_NTSC
#define VO_WIDTH            1920
#define VO_HEIGHT           1080
#define VO_uiCHN            2
#else
#define VO_intfTYPE         VO_INTF_LCD
#define VO_intfSYNC         VO_OUTPUT_NTSC
#define VO_WIDTH            720
#define VO_HEIGHT           1280
#define VO_uiCHN            2
#endif

#define PLAY_statIDLE       0
#define PLAY_statOPENED     BIT(0)
#define PLAY_statSTARTED    BIT(1)
#define PLAY_statPAUSED     BIT(2)
#define PLAY_statCOMPLETED  BIT(3)

#define PLAY_statSEEKING    BIT(16)

typedef struct
{
    Vdec2VoContext_t* vv;
    AwdmxContext_t*   dmx;
    uint32_t          state;
    pthread_mutex_t   mutex;

    int               playingTime;    //ms
} PlayContext_t;

static int play_start(PlayContext_t* playCtx)
{
    int ret = vdec2vo_start(playCtx->vv);

    if( ret == SUCCESS ) {
        ret = awdmx_start(playCtx->dmx);
    }

    playCtx->state &= ~(PLAY_statPAUSED|PLAY_statCOMPLETED);
    playCtx->state |= PLAY_statSTARTED;

    return ret;
}

static int play_pause(PlayContext_t* playCtx)
{
    int ret = awdmx_pause(playCtx->dmx);

    if( ret == SUCCESS ) {
        ret = vdec2vo_pause(playCtx->vv);
    }

    playCtx->state |= PLAY_statPAUSED;

    return ret;
}

static int play_stop(PlayContext_t* playCtx)
{
    int ret = awdmx_stop(playCtx->dmx);

    if( ret == SUCCESS ) {
         ret = vdec2vo_stop(playCtx->vv);
    }

    return ret;
}

static int play_seekto(PlayContext_t* playCtx, int seekTime)
{
    int ret;

    ret = awdmx_seekTo(playCtx->dmx, seekTime);

    if( ret == SUCCESS ) {
        ret = vdec2vo_seekTo(playCtx->vv);
    }

    return ret;
}

static void play_moveStatus(PlayContext_t* playCtx)
{
    if( !(playCtx->state & PLAY_statSTARTED) ) {
       return;
    }

    if( !(playCtx->state & PLAY_statCOMPLETED) ) {
        if( awdmx_isEOF(playCtx->dmx) ) {
            vdec2vo_checkEof(playCtx->vv);
        }

        if( vdec2vo_isEOF(playCtx->vv) ) {
            play_stop(playCtx);
            playCtx->state |= PLAY_statCOMPLETED;
        }
    }
}
void *thread_media(void *params)
{
	media_info_t info;
	media_t *media = (media_t *)params;
	PlayContext_t *playCtx = (PlayContext_t *)media->context;
	notify_cb_t notify = media->notify;
	for(;;)
	{
        if(!media) break;
        //if(media->is_media_thread_exit) 
        //    printf("is_media_thread_exit = 1\n");

		if(media->is_media_thread_exit)
			break;

        pthread_mutex_lock(&playCtx->mutex);
		play_moveStatus(playCtx);
		if( !vdec2vo_isEOF(playCtx->vv) ) {
		    vdec2vo_currentMediaTime(playCtx->vv, &playCtx->playingTime);
		}
		else
		{
			playCtx->playingTime = -2;
		}
        pthread_mutex_unlock(&playCtx->mutex);

        //if(media->is_media_thread_exit) 
        //    printf("is_media_thread_exit = 2\n");

		info.playing_time =playCtx->playingTime; 
		info.duration =playCtx->dmx->msDuration; 
		if(!media->is_media_thread_exit)
            notify(&info);
		usleep(100000);
	}
	return NULL;
}

void media_control(media_t *media, player_cmd_t *cmd)
{
	PlayContext_t *playCtx =(PlayContext_t *)media->context; 

    pthread_mutex_lock(&playCtx->mutex);

	switch(cmd->opt)
	{
		case PLAYER_START: 
			play_start(playCtx);
			break;
		case PLAYER_STOP: 
			play_stop(playCtx);
			break;
		case PLAYER_PAUSE: 
			play_pause(playCtx);
			break;
		case PLAYER_SEEK: 
			play_seekto(playCtx, cmd->params);
			break;

		default:
			break;
	}

    pthread_mutex_unlock(&playCtx->mutex);
}

media_t *media_instantiate(char *filename, notify_cb_t notify)
{
    int ret = 0;
	pthread_t pid;

    PlayContext_t *playCtx = calloc(1, sizeof(PlayContext_t));
	if(!playCtx)
		return NULL;

	media_t *media = calloc(1, sizeof(media_t));
	if(!media)
		return NULL;
	media->context = playCtx;
	media->notify = notify;

    pthread_mutex_init(&playCtx->mutex, NULL);

    playCtx->vv = vdec2vo_initSys();
    if( playCtx->vv == NULL ) {
        aloge("create vdec2vo failed");
        goto failed;
    }

    playCtx->dmx = awdmx_open(filename);
    if( playCtx->dmx == NULL ) {
        aloge("create vdec2vo failed");
        goto failed;
    }
    else {
        Vdec2VoParams_t vvParams;
        memset(&vvParams, 0, sizeof(vvParams));

        vvParams.initRotation = 0;
        vvParams.pixelFormat = MM_PIXEL_FORMAT_YVU_PLANAR_420;

        vvParams.vdec.codecType = playCtx->dmx->codecType;
        vvParams.vdec.width = playCtx->dmx->width;
        vvParams.vdec.height= playCtx->dmx->height;

        vvParams.vo.width = VO_WIDTH;
        vvParams.vo.height= VO_HEIGHT;
        vvParams.vo.intfType = VO_intfTYPE;
        vvParams.vo.intfSync = VO_intfSYNC;
        vvParams.vo.uiChn = VO_uiCHN;

        ret = vdec2vo_prepare(playCtx->vv, &vvParams);
    }
    if( ret != 0 ) {
        aloge("prepare vdec2vo failed");
        goto failed;
    }

    awdmx_bindVdecAndClock(playCtx->dmx, playCtx->vv->vdecChn, playCtx->vv->clkChn);

    aloge("ready to play");
	media->is_media_thread_exit = false;
	ret = pthread_create( &pid, NULL, thread_media, (void *)media);
	if(ret)
	{
		aloge("create thread media failed, exit");
		return NULL;
	}
	media->pid = pid;
	return media;
failed:
    awdmx_close(playCtx->dmx);
    vdec2vo_deinitSys(playCtx->vv);
    pthread_mutex_destroy(&playCtx->mutex);
    free(playCtx);
    alogd("exit done");

    return NULL;
}

void media_exit(media_t *media)
{
	assert(media);
	media->is_media_thread_exit = true;
    pthread_join(media->pid, NULL);

	PlayContext_t * playCtx = (PlayContext_t *)media->context; 
    play_stop(playCtx);
    awdmx_close(playCtx->dmx);
    vdec2vo_deinitSys(playCtx->vv);
    pthread_mutex_destroy(&playCtx->mutex);
	free(media->context);
    free(media);
}
