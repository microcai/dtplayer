#include "dtaudio.h"
#include "dthost_api.h"

#define TAG "AUDIO"

//==Part1:Data IO Relative
/*read frame from dtport*/
int audio_read_frame (void *priv, dt_av_frame_t * frame)
{
    int type = DT_TYPE_AUDIO;
    int ret = 0;
    dtaudio_context_t *actx = (dtaudio_context_t *) priv;
    ret = dthost_read_frame (actx->parent, frame, type);
    return ret;
}

int audio_filter_read ()
{
    return 0;
}

/*ao read pcm from decoder buf*/
int audio_output_read (void *priv, uint8_t * buf, int size)
{
    dtaudio_context_t *actx = (dtaudio_context_t *) priv;
    return buf_get (&actx->audio_decoded_buf, buf, size);
}

//==Part2: PTS&STATUS Relative
int64_t audio_get_current_pts (dtaudio_context_t * actx)
{
    int64_t pts, delay_pts;
    if (actx->audio_state < AUDIO_STATUS_INITED)
        return -1;
    pts = audio_decoder_get_pts (&actx->audio_dec);
    if (pts == -1)
        return -1;
    delay_pts = audio_output_get_latency (&actx->audio_out);
    if (delay_pts == -1)
        delay_pts = 0;
    if (delay_pts < pts)
        pts -= delay_pts;
    else
        pts = 0;
    return pts;
}

int64_t audio_get_first_pts (dtaudio_context_t * actx)
{
    if (actx->audio_state != AUDIO_STATUS_INITED)
        return -1;
    return actx->audio_dec.pts_first;
}

int audio_drop (dtaudio_context_t * actx, int64_t target_pts)
{
    int64_t diff = target_pts - audio_get_first_pts (actx);

    int diff_time = diff / 90;
    if (diff_time > AV_DROP_THRESHOLD)
    {
        dt_info (TAG, "DIFF EXCEED %d ms, do not drop!\n", diff_time);
        return 0;
    }
    int channel = actx->audio_param.channels;
    int sample_rate = actx->audio_param.samplerate;
    int bps = actx->audio_param.bps;
    int size = (diff * sample_rate * bps * channel / 8) / 90000;

    dt_info (TAG, "[%s:%d]Target:%lld ,Need to drop :%d pcm \n", __FUNCTION__, __LINE__, target_pts, size);
    int read_size_once = 1024;
    uint8_t buf[1024];
    int rlen = 0;
    int drop_count = 50;
    int64_t pts = -1;
    while (size > 0)
    {
        rlen = audio_output_read ((void *) actx, buf, (size > read_size_once) ? read_size_once : size);
        if (rlen == -1)
        {
            if (drop_count-- == 0)
                break;
            usleep (1000);
            continue;
        }
        drop_count = 50;
        size -= rlen;
        /*calc pts every time, for interrupt */
        pts = audio_get_current_pts (actx);
        if (pts >= target_pts)
            break;
        dt_debug (TAG, "read :%d pcm left:%d pts:%lld \n", rlen, size, pts);
    }
    audio_update_pts ((void *) actx);
    dt_info (TAG, "drop finish,size:%d left. \n", size);
    return 0;
}

/*$ update dtcore audio pts*/
void audio_update_pts (void *priv)
{
    int64_t pts, delay_pts;
    dtaudio_context_t *actx = (dtaudio_context_t *) priv;
    if (actx->audio_state < AUDIO_STATUS_INITED)
        return;
    pts = audio_decoder_get_pts (&actx->audio_dec);
    if (pts == -1)
        return;
    delay_pts = audio_output_get_latency (&actx->audio_out);
    if (delay_pts == -1)
        delay_pts = 0;
    dt_debug (TAG, "[%s:%d] pts:%lld  delay:%lld  actual:%lld time:%d \n", __FUNCTION__, __LINE__, pts, delay_pts, pts - delay_pts, (pts - delay_pts) / 90000);
    if (delay_pts < pts)
        pts -= delay_pts;
    else
        pts = 0;
    dthost_update_apts (actx->parent, pts);
    return;
}

int audio_get_dec_state (dtaudio_context_t * actx, dec_state_t * dec_state)
{
    if (actx->audio_state <= AUDIO_STATUS_INITED)
        return -1;
    dtaudio_decoder_t *adec = &actx->audio_dec;
    dec_state->adec_channels = adec->aparam.channels;
    dec_state->adec_error_count = adec->decode_err_cnt;
    dec_state->adec_bps = adec->aparam.bps;
    dec_state->adec_sample_rate = adec->aparam.samplerate;
    dec_state->adec_status = adec->status;
    return 0;
}

//lower than 50ms, return true
int audio_get_out_closed (dtaudio_context_t * actx)
{
    if (actx->audio_state <= AUDIO_STATUS_INITED)
        return 0;
    int default_level;
    int decoder_level, output_level, total;
    //get output buffer level
    output_level = audio_output_get_level (&actx->audio_out);
    //get decoder buffer level
    decoder_level = actx->audio_decoded_buf.level;
    total = decoder_level + output_level;
    dt_info (TAG, "[%s:%d] decode_level:%d output_level:%d total:%d \n", __FUNCTION__, __LINE__, decoder_level, output_level, total);
    default_level = actx->audio_param.samplerate * actx->audio_param.bps * actx->audio_param.channels / 8;
    if (total <= default_level / 20)
    {
        dt_info (TAG, "[%s:%d] decode_level:%d output_level:%d total:%d \n", __FUNCTION__, __LINE__, decoder_level, output_level, total);
        return 1;
    }
    return 0;
}

//==Part3:Control
void audio_register_all()
{
    adec_register_all();
    aout_register_all();
}

int audio_start (dtaudio_context_t * actx)
{
    if (actx->audio_state == AUDIO_STATUS_INITED)
    {
        dtaudio_output_t *audio_out = &actx->audio_out;
        audio_output_start (audio_out);
        actx->audio_state = AUDIO_STATUS_ACTIVE;
        return 0;
    }
    dt_error (TAG, "[%s:%d]audio output start failed \n", __FUNCTION__, __LINE__);
    return -1;
}

int audio_pause (dtaudio_context_t * actx)
{
    if (actx->audio_state == AUDIO_STATUS_ACTIVE)
    {
        dtaudio_output_t *audio_out = &actx->audio_out;
        audio_output_pause (audio_out);
        actx->audio_state = AUDIO_STATUS_PAUSED;
    }
    return 0;
}

int audio_resume (dtaudio_context_t * actx)
{
    if (actx->audio_state == AUDIO_STATUS_PAUSED)
    {
        dtaudio_output_t *audio_out = &actx->audio_out;
        audio_output_resume (audio_out);
        actx->audio_state = AUDIO_STATUS_ACTIVE;
    }
    return 0;
}

int audio_stop (dtaudio_context_t * actx)
{
    if (actx->audio_state > AUDIO_STATUS_INITED)
    {
        dtaudio_output_t *audio_out = &actx->audio_out;
        audio_output_stop (audio_out);
        dtaudio_filter_t *audio_filter = &actx->audio_filt;
        audio_filter_stop (audio_filter);
        dtaudio_decoder_t *audio_decoder = &actx->audio_dec;
        audio_decoder_stop (audio_decoder);
        actx->audio_state = AUDIO_STATUS_STOP;
    }
    return 0;
}

static void *event_handle_loop (void *args)
{
    dtaudio_context_t *actx;
    actx = (dtaudio_context_t *) args;
    event_t *event = NULL;
    event_server_t *server = (event_server_t *) actx->audio_server;

    dt_info (TAG, "[%s:%d] dtaudio init ok, enter event handle loop\n", __FUNCTION__, __LINE__);
    do
    {
        if (actx->audio_state == AUDIO_STATUS_STOP)
            break;

        event = dt_get_event (server);
        if (!event)
        {
            usleep (10000);
            continue;
        }

        switch (event->type)
        {
        case AUDIO_EVENT_START:

            dt_info (DTAUDIO_LOG_TAG, "Receive START Command!\n");
            audio_start (actx);
            break;

        case AUDIO_EVENT_PAUSE:

            dt_info (DTAUDIO_LOG_TAG, "Receive PAUSE Command!\n");
            audio_pause (actx);
            break;

        case AUDIO_EVENT_RESUME:

            dt_info (DTAUDIO_LOG_TAG, "Receive RESUME Command!\n");
            audio_resume (actx);
            break;

        case AUDIO_EVENT_STOP:

            dt_info (DTAUDIO_LOG_TAG, "Receive STOP Command!\n");
            audio_stop (actx);
            break;

        case AUDIO_EVENT_MUTE:

            dt_info (DTAUDIO_LOG_TAG, "Receive Mute Command!\n");
            break;

        default:
            dt_info (DTAUDIO_LOG_TAG, "Unknow Command!\n");
            break;
        }

        if (event)
        {
            free (event);
            event = NULL;
        }
    }
    while (1);

    dt_info (DTAUDIO_LOG_TAG, "Exit Event Handle Loop Thread!\n");
    pthread_exit (NULL);
    return NULL;

}

int audio_init (dtaudio_context_t * actx)
{
    int ret = 0;
    pthread_t tid;
    dt_info (DTAUDIO_LOG_TAG, "[%s:%d] audio init start\n", __FUNCTION__, __LINE__);

    actx->audio_state = AUDIO_STATUS_INITING;
    dtaudio_decoder_t *audio_dec = &actx->audio_dec;
    dtaudio_filter_t *audio_filter = &actx->audio_filt;
    dtaudio_output_t *audio_out = &actx->audio_out;

    /*audio decoder init */
    memset (audio_dec, 0, sizeof (dtaudio_decoder_t));
    memset (&audio_dec->aparam, 0, sizeof (dtaudio_para_t));
    memcpy (&audio_dec->aparam, &actx->audio_param, sizeof (dtaudio_para_t));
    audio_dec->parent = actx;
    ret = audio_decoder_init (audio_dec);
    if (ret < 0)
        goto err1;

    /*audio filter decoder */
    memset (audio_filter, 0, sizeof (dtaudio_filter_t));
    ret = audio_filter_init (audio_filter);
    if (ret < 0)
        goto err2;

    /*audio output decoder */
    memset (audio_out, 0, sizeof (dtaudio_output_t));
    memcpy (&(audio_out->para), &(actx->audio_param), sizeof (audio_out->para));
    audio_out->parent = actx;
    ret = audio_output_init (audio_out, actx->audio_param.audio_output);
    if (ret < 0)
        goto err3;
    /*create event handle thread */
    ret = pthread_create (&tid, NULL, (void *) event_handle_loop, (void *) actx);
    if (ret != 0)
    {
        dt_info (DTAUDIO_LOG_TAG, "create dtaudio thread failed\n");
        goto err3;
    }
    dt_info (DTAUDIO_LOG_TAG, "[%s:%d] audio init ok \n", __FUNCTION__, __LINE__);
    actx->event_loop_id = tid;
    actx->audio_state = AUDIO_STATUS_INITED;

    return 0;
  err1:
    dt_info (DTAUDIO_LOG_TAG, "[%s:%d]audio decoder init failed \n", __FUNCTION__, __LINE__);
    return -1;
  err2:
    audio_decoder_stop (audio_dec);
    dt_info (DTAUDIO_LOG_TAG, "[%s:%d]audio filter init failed \n", __FUNCTION__, __LINE__);
    return -2;
  err3:
    audio_decoder_stop (audio_dec);
    audio_filter_stop (audio_filter);
    dt_info (DTAUDIO_LOG_TAG, "[%s:%d]audio output init failed \n", __FUNCTION__, __LINE__);
    return -3;

}
