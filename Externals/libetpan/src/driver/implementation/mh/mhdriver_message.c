/*
 * libEtPan! -- a mail stuff library
 *
 * Copyright (C) 2001, 2005 - DINH Viet Hoa
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the libEtPan! project nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * $Id: mhdriver_message.c,v 1.23 2008/02/17 13:13:26 hoa Exp $
 */

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif

#include "mhdriver_message.h"

#include "mailmessage_tools.h"
#include "mhdriver_tools.h"
#include "mhdriver.h"
#include "mailmh.h"

#ifdef HAVE_UNISTD_H
#	include <unistd.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#	include <sys/mman.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>

static int mh_prefetch(mailmessage * msg_info);

static void mh_prefetch_free(struct generic_message_t * msg);

static int mh_initialize(mailmessage * msg_info);

static int mh_fetch_size(mailmessage * msg_info,
			 size_t * result);

static int mh_fetch_header(mailmessage * msg_info,
			   char ** result,
			   size_t * result_len);

static mailmessage_driver local_mh_message_driver = {
  /* msg_name */ "mh",

  /* msg_initialize */ mh_initialize,
  /* msg_uninitialize */ mailmessage_generic_uninitialize,

  /* msg_flush */ mailmessage_generic_flush,
  /* msg_check */ NULL,

  /* msg_fetch_result_free */ mailmessage_generic_fetch_result_free,

  /* msg_fetch */ mailmessage_generic_fetch,
  /* msg_fetch_header */ mh_fetch_header,
  /* msg_fetch_body */ mailmessage_generic_fetch_body,
  /* msg_fetch_size */ mh_fetch_size,
  /* msg_get_bodystructure */ mailmessage_generic_get_bodystructure,
  /* msg_fetch_section */ mailmessage_generic_fetch_section,
  /* msg_fetch_section_header */ mailmessage_generic_fetch_section_header,
  /* msg_fetch_section_mime */ mailmessage_generic_fetch_section_mime,
  /* msg_fetch_section_body */ mailmessage_generic_fetch_section_body,
  /* msg_fetch_envelope */ mailmessage_generic_fetch_envelope,

  /* msg_get_flags */ NULL
};

mailmessage_driver * mh_message_driver = &local_mh_message_driver;

static int mh_prefetch(mailmessage * msg_info)
{
  struct generic_message_t * msg;
  int r;
  char * msg_content;
  size_t msg_length;

  r = mhdriver_fetch_message(msg_info->msg_session, msg_info->msg_index,
			     &msg_content, &msg_length);
  if (r != MAIL_NO_ERROR)
    return r;

  msg = msg_info->msg_data;

  msg->msg_message = msg_content;
  msg->msg_length = msg_length;

  return MAIL_NO_ERROR;
}

static void mh_prefetch_free(struct generic_message_t * msg)
{
  if (msg->msg_message != NULL) {
    mmap_string_unref(msg->msg_message);
    msg->msg_message = NULL;
  }
}

static inline struct mh_session_state_data * get_data(mailmessage * msg)
{
  return msg->msg_session->sess_data;
}

static inline struct mailmh_folder * get_mh_cur_folder(mailmessage * msg)
{
  return get_data(msg)->mh_cur_folder;
}

static int mh_initialize(mailmessage * msg_info)
{
  struct generic_message_t * msg;
  int r;
  char * uid;
  char static_uid[PATH_MAX];
  struct mailmh_msg_info * mh_msg_info;
  chashdatum key;
  chashdatum value;
  
  key.data = &msg_info->msg_index;
  key.len = sizeof(msg_info->msg_index);
  r = chash_get(get_mh_cur_folder(msg_info)->fl_msgs_hash, &key, &value);
  if (r < 0)
    return MAIL_ERROR_INVAL;
  
  mh_msg_info = value.data;
  
  snprintf(static_uid, PATH_MAX, "%u-%lu-%lu", msg_info->msg_index,
	   (unsigned long) mh_msg_info->msg_mtime,
      (unsigned long) mh_msg_info->msg_size);
  uid = strdup(static_uid);
  if (uid == NULL)
    return MAIL_ERROR_MEMORY;

  r = mailmessage_generic_initialize(msg_info);
  if (r != MAIL_NO_ERROR) {
    free(uid);
    return r;
  }

  msg = msg_info->msg_data;
  msg->msg_prefetch = mh_prefetch;
  msg->msg_prefetch_free = mh_prefetch_free;
  msg_info->msg_uid = uid;

  return MAIL_NO_ERROR;
}

     
static int mh_fetch_size(mailmessage * msg_info,
			 size_t * result)
{
  int r;
  size_t size;

  r = mhdriver_fetch_size(msg_info->msg_session, msg_info->msg_index, &size);
  if (r != MAIL_NO_ERROR)
    return r;
  
  * result = size;

  return MAIL_NO_ERROR;
}




static int mh_fetch_header(mailmessage * msg_info,
			   char ** result,
			   size_t * result_len)
{
  struct generic_message_t * msg;
  int r;
  char * msg_content;
  size_t msg_length;

  msg = msg_info->msg_data;
  if (msg->msg_message != NULL) {
    
    r = mailmessage_generic_fetch_header(msg_info, result, result_len);
    return r;
  }
  else {
    r = mhdriver_fetch_header(msg_info->msg_session, msg_info->msg_index,
        &msg_content, &msg_length);
    if (r != MAIL_NO_ERROR)
      return r;
    
    * result = msg_content;
    * result_len = msg_length;
    
    return MAIL_NO_ERROR;
  }
}
