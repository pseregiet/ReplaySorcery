/*
 * Copyright (C) 2020  Joshua Minter & Patryk Seregiet
 *
 * This file is part of ReplaySorcery.
 *
 * ReplaySorcery is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ReplaySorcery is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ReplaySorcery.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "audio.h"
#include "util/log.h"
#include "util/memory.h"

static void rsAudioReadSamples(RSAudio *audio);
static void rsAudioPrepareEncoder(RSAudio *audio, const RSConfig* config);
static void rsAudioPrepareEncoder(RSAudio *audio, const RSConfig* config);
static void rsAudioEncoderPrepareFrame(RSAudioEncoder *audioenc);

extern sig_atomic_t mainRunning;
extern sig_atomic_t wantToSave;

int rsAudioCreate(RSAudio *audio, const RSConfig *config) {
   int error;
   const pa_sample_spec ss = {
       .format = PA_SAMPLE_S16LE, 
       .rate = (unsigned)config->audioSamplerate, 
       .channels = (unsigned char)config->audioChannels
   };
   audio->pa_api = pa_simple_new(NULL, "Replay Sorcery", PA_STREAM_RECORD, NULL,
                                 "record", &ss, NULL, NULL, &error);

   if (!audio->pa_api) {
      rsError("PulseAudio: pa_simple_new() failed: %s\n", pa_strerror(error));
      return 0;
   }
   size_t size_1s = pa_bytes_per_second(&ss);
   size_t size_per_frame = size_1s / (unsigned)config->framerate;
   size_t size_total = size_1s * (unsigned)config->duration;
   audio->sizebatch = size_per_frame;
   audio->size = size_total;
   audio->data = rsMemoryCreate(size_total);
   audio->index = 0;
   rsAudioPrepareEncoder(audio, config);
   return 1;
}


void rsAudioEncoderCreate(RSAudioEncoder* audioenc, const RSAudio *audio, size_t rewindframes) {
	*audioenc = audio->audioenc;
	audioenc->data = rsMemoryCreate(audioenc->size);
	audioenc->frame = rsMemoryCreate(audioenc->frame_size); 
	memcpy(audioenc->data, audio->data, audio->size);
	size_t rewind_bytes = rewindframes * audio->sizebatch;
	if (rewind_bytes <= audio->index) {
		audioenc->index = audio->index - rewind_bytes;
		return;
	}
	audioenc->index = audio->size - (rewind_bytes - audio->index);
}

void rsAudioDestroy(RSAudio *audio) {
   if (audio->pa_api) {
      pa_simple_free(audio->pa_api);
   }
   if (audio->data) {
      free(audio->data);
   }
}

void rsAudioEncoderDestroy(RSAudioEncoder* audioenc) {
	if (audioenc->data) {
		rsMemoryDestroy(audioenc->data);
	}
	if (audioenc->frame) {
		rsMemoryDestroy(audioenc->frame);
	}
}

void rsAudioEncodeFrame(RSAudioEncoder *audioenc, uint8_t *out, int *num_of_bytes, int *num_of_samples) {
   AACENC_BufDesc ibuf = {0};
   AACENC_BufDesc obuf = {0};
   AACENC_InArgs iargs = {0};
   AACENC_OutArgs oargs = {0};

   rsAudioEncoderPrepareFrame(audioenc);
   void *iptr = audioenc->frame;
   void *optr = out;

   int ibufsizes = audioenc->frame_size;
   int iidentifiers = IN_AUDIO_DATA;
   int ielsizes = 2;
   int obufsizes = 2048;
   int oidentifiers = OUT_BITSTREAM_DATA;
   int oelsizes = 1;

   iargs.numInSamples = audioenc->samples_per_frame;
   ibuf.numBufs = 1;
   ibuf.bufs = &iptr;
   ibuf.bufferIdentifiers = &iidentifiers;
   ibuf.bufSizes = &ibufsizes;
   ibuf.bufElSizes = &ielsizes;

   obuf.numBufs = 1;
   obuf.bufs = &optr;
   obuf.bufferIdentifiers = &oidentifiers;
   obuf.bufSizes = &obufsizes;
   obuf.bufElSizes = &oelsizes;

   if (AACENC_OK !=
       aacEncEncode(audioenc->aac_enc, &ibuf, &obuf, &iargs, &oargs)) {
      rsLog("AAC: aac encode failed");
   }
   *num_of_bytes = oargs.numOutBytes;
   *num_of_samples = iargs.numInSamples;
}

void *rsAudioThread(void *data) {
   RSAudio *audio = (RSAudio *)data;
   while (!wantToSave && mainRunning) {
      rsAudioReadSamples(audio);
   }
   return 0;
}

static void rsAudioReadSamples(RSAudio *audio) {
   int error;
   int ret = pa_simple_read(audio->pa_api, audio->data + audio->index, audio->sizebatch,
                        &error);
   if (ret < 0) {
      rsError("PulseAudio: pa_simple_read() failed: %s", pa_strerror(error));
      return;
   }
   audio->index += audio->sizebatch;
   if (audio->index >= audio->size) {
      audio->index -= audio->size;
   }
}

static void rsAudioPrepareEncoder(RSAudio *audio, const RSConfig* config) {
   RSAudioEncoder* audioenc = &audio->audioenc;
   
   aacEncOpen(&(audioenc->aac_enc), 0, 0);
   aacEncoder_SetParam(audioenc->aac_enc, AACENC_TRANSMUX, 0);
   aacEncoder_SetParam(audioenc->aac_enc, AACENC_AFTERBURNER, 1);
   aacEncoder_SetParam(audioenc->aac_enc, AACENC_BITRATE, config->audioBitrate);
   aacEncoder_SetParam(audioenc->aac_enc, AACENC_SAMPLERATE, config->audioSamplerate);
   aacEncoder_SetParam(audioenc->aac_enc, AACENC_CHANNELMODE, config->audioChannels);
   aacEncEncode(audioenc->aac_enc, NULL, NULL, NULL, NULL);
   aacEncInfo(audioenc->aac_enc, &(audio->audioenc.aac_info));

   audioenc->data = 0;
   audioenc->frame = 0;
   audioenc->index = 0;
   audioenc->size = audio->size;
   audioenc->samples_per_frame = audioenc->aac_info.frameLength * config->audioChannels;
   audioenc->frame_size = audioenc->samples_per_frame * sizeof(uint16_t);
}

static void rsAudioEncoderPrepareFrame(RSAudioEncoder *audioenc) {
   int32_t first_copy_size = audioenc->frame_size;
   int32_t end_of_data = audioenc->index + audioenc->frame_size;
   int diff = audioenc->size - end_of_data;
   if (diff >= 0) {
      memcpy(audioenc->frame, audioenc->data + audioenc->index, first_copy_size);
      audioenc->index += first_copy_size;
      return;
   }
   first_copy_size += diff;
   memcpy(audioenc->frame, audioenc->data + audioenc->index, first_copy_size);
   memcpy(audioenc->frame + first_copy_size, audioenc->data, -diff);
   audioenc->index = -diff;
}

