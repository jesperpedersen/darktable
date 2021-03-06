/*
    This file is part of darktable,
    copyright (c) 2010 -- 2014 Henrik Andersson.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "common/darktable.h"
#include "common/image_cache.h"
#include "control/jobs/image_jobs.h"

typedef struct dt_image_load_t
{
  int32_t imgid;
  dt_mipmap_size_t mip;
}
dt_image_load_t;

static int32_t dt_image_load_job_run(dt_job_t *job)
{
  dt_image_load_t *params = dt_control_job_get_params(job);

  // hook back into mipmap_cache:
  dt_mipmap_buffer_t buf;
  dt_mipmap_cache_read_get(
    darktable.mipmap_cache,
    &buf,
    params->imgid,
    params->mip,
    DT_MIPMAP_BLOCKING);

  // drop read lock, as this is only speculative async loading.
  if(buf.buf)
    dt_mipmap_cache_read_release(darktable.mipmap_cache, &buf);
  free(params);
  return 0;
}

dt_job_t * dt_image_load_job_create(int32_t id, dt_mipmap_size_t mip)
{
  dt_job_t *job = dt_control_job_create(&dt_image_load_job_run, "load image %d mip %d", id, mip);
  if(!job) return NULL;
  dt_image_load_t *params = (dt_image_load_t *)calloc(1, sizeof(dt_image_load_t));
  if(!params)
  {
    dt_control_job_dispose(job);
    return NULL;
  }
  dt_control_job_set_params(job, params);
  params->imgid = id;
  params->mip = mip;
  return job;
}

typedef struct dt_image_import_t
{
  uint32_t film_id;
  const char *filename;
}
dt_image_import_t;

static int32_t dt_image_import_job_run(dt_job_t *job)
{
  int id;
  char message[512];
  dt_image_import_t *params;
  const guint *jid;

  params = dt_control_job_get_params(job);
  message[0] = 0;

  snprintf(message, sizeof(message), _("importing image %s"), params->filename);
  jid = dt_control_backgroundjobs_create(darktable.control, 0, message );

  id = dt_image_import(params->film_id, params->filename, TRUE);
  if(id)
  {
    dt_view_filmstrip_set_active_image(darktable.view_manager, id);
    dt_control_queue_redraw();
  }

  dt_control_backgroundjobs_progress(darktable.control, jid, 1.0);
  dt_control_backgroundjobs_destroy(darktable.control, jid);
  free(params);
  return 0;
}

dt_job_t * dt_image_import_job_create(uint32_t filmid, const char *filename)
{
  dt_image_import_t *params;
  dt_job_t *job = dt_control_job_create(&dt_image_import_job_run, "import image");
  if(!job) return NULL;
  params = (dt_image_import_t *)calloc(1, sizeof(dt_image_import_t));
  if(!params)
  {
    dt_control_job_dispose(job);
    return NULL;
  }
  dt_control_job_set_params(job, params);
  params->filename = g_strdup(filename);
  params->film_id = filmid;
  return job;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
