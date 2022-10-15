/*------------------------------------------------------------------------------
--       Copyright (c) 2015-2017, VeriSilicon Inc. All rights reserved        --
--         Copyright (c) 2011-2014, Google Inc. All rights reserved.          --
--         Copyright (c) 2007-2010, Hantro OY. All rights reserved.           --
--                                                                            --
-- This software is confidential and proprietary and may be used only as      --
--   expressly authorized by VeriSilicon in a written licensing agreement.    --
--                                                                            --
--         This entire notice must be reproduced on all copies                --
--                       and may not be removed.                              --
--                                                                            --
--------------------------------------------------------------------------------
-- Redistribution and use in source and binary forms, with or without         --
-- modification, are permitted provided that the following conditions are met:--
--   * Redistributions of source code must retain the above copyright notice, --
--       this list of conditions and the following disclaimer.                --
--   * Redistributions in binary form must reproduce the above copyright      --
--       notice, this list of conditions and the following disclaimer in the  --
--       documentation and/or other materials provided with the distribution. --
--   * Neither the names of Google nor the names of its contributors may be   --
--       used to endorse or promote products derived from this software       --
--       without specific prior written permission.                           --
--------------------------------------------------------------------------------
-- THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"--
-- AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE  --
-- IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE --
-- ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE  --
-- LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR        --
-- CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF       --
-- SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS   --
-- INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN    --
-- CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)    --
-- ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE --
-- POSSIBILITY OF SUCH DAMAGE.                                                --
--------------------------------------------------------------------------------
------------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vp8writeoutput.h"
#include "testparams.h"
#include "md5.h"

typedef struct output_s {
  u8 *frame_pic_;
  test_params *params;
  FILE *file_;
} output_t;

static void FramePicture( u8 *p_in, u8* p_ch, i32 in_width, i32 in_height,
                          i32 in_frame_width, i32 in_frame_height,
                          u8 *p_out, i32 out_width, i32 out_height,
                          u32 luma_stride, u32 chroma_stride );

output_inst output_open(char* filename, test_params *params) {

  output_t* inst = calloc(1, sizeof(output_t));
  if (inst==NULL)
    return NULL;

  if(params==NULL) {
    if (inst)
      free(inst);
    return NULL;
  }

  inst->params = params;

  if(!params->disable_write_) {
    inst->file_ = fopen(filename, "wb");
    if (inst->file_ == NULL) {
      free(inst);
      return NULL;
    }
  } else {
    inst->file_ = NULL;
  }
  return inst;

}

void output_close(output_inst inst) {
  output_t* output = (output_t*)inst;
  if(output->file_ != NULL) {
    fclose(output->file_);
  }
  if(output->frame_pic_ != NULL) {
    free(output->frame_pic_);
  }

  free(output);
}

static void FramePicture( u8 *p_in, u8* p_ch, i32 in_width, i32 in_height,
                          i32 in_frame_width, i32 in_frame_height,
                          u8 *p_out, i32 out_width, i32 out_height,
                          u32 luma_stride, u32 chroma_stride ) {

  /* Variables */

  i32 x, y;

  /* Code */
  memset( p_out, 0, out_width*out_height );
  memset( p_out+out_width*out_height, 128, out_width*out_height/2 );

  /* Luma */
  for ( y = 0 ; y < in_height ; ++y ) {
    for( x = 0 ; x < in_width; ++x )
      *p_out++ = *p_in++;
    p_in += ( luma_stride - in_width );
    p_out += ( out_width - in_width );
  }

  p_out += out_width * ( out_height - in_height );
  if(p_ch)
    p_in = p_ch;
  else
    p_in += luma_stride * ( in_frame_height - in_height );

  in_frame_height /= 2;
  in_frame_width /= 2;
  out_height /= 2;
  out_width /= 2;
  in_height /= 2;
  in_width /= 2;
  /* Chroma */
  for ( y = 0 ; y < in_height ; ++y ) {
    for( x = 0 ; x < 2*in_width; ++x )
      *p_out++ = *p_in++;
    p_in += 2 * ( (chroma_stride/2) - in_width );
    p_out += 2 * ( out_width - in_width );
  }
}
