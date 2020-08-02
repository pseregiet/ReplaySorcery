/*
 * Copyright (C) 2020  Patryk Seregiet
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

#ifndef RS_UTIL_AUDIO_H
#define RS_UTIL_AUDIO_H

#include "config.h"
#include "util/circle_static.h"
#include <pthread.h>
#include <fdk-aac/aacenc_lib.h>
#include <SDL2/SDL.h>

#define AUDIO_RATE 44100
#define AUDIO_CHANNELS 1
#define AUDIO_BITRATE 96000

typedef struct RSAudioEncoder {
   HANDLE_AACENCODER aac_enc;
   AACENC_InfoStruct aac_info;
   RSCircleStatic data;
   uint8_t *frame;
   int size;
   int frameSize;
   int index;
   int samplesPerFrame;
} RSAudioEncoder;


typedef struct RSAudio {
   RSAudioEncoder audioenc;
   SDL_AudioDeviceID deviceId;
   pthread_spinlock_t sampleGetLock;
   RSCircleStatic data;
   int bitrate;
   int channels;
   int size;
   int index;
   int sizeBatch;
} RSAudio;

void rsAudioEncodeFrame(RSAudioEncoder *audioenc, uint8_t *out, int *numBytes, int *numSamples);
void rsAudioEncoderCreate(RSAudioEncoder* audioenc, RSAudio *audio, int rewindFrames);
void rsAudioEncoderDestroy(RSAudioEncoder* audioenc);

void rsAudioCreate(RSAudio *audio, const RSConfig *config);
void rsAudioDestroy(RSAudio *audio);
void rsAudioReadSamples(RSAudio *audio);

#endif

