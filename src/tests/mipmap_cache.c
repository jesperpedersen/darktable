/*
    This file is part of darktable,
    copyright (c) 2011 johannes hanika.

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
#include "control/conf.h"

static inline uint32_t
get_key(const uint32_t imgid, const dt_mipmap_size_t size)
{
  // imgid can't be >= 2^29 (~500 million images)
  return (size << 29) | imgid;
}

static inline uint32_t
get_imgid(const uint32_t key)
{
  return key & 0x1fffffff;
}

static inline dt_mipmap_size_t
get_size(const uint32_t key)
{
  return key >> 29;
}

// TODO: cache read/write functions!
// see old common/image_cache.c for backup file magic.

void*
dt_mipmap_cache_allocate(void *data, const uint32_t key, int32_t *cost)
{
  dt_mipmap_cache_one_t *c = (dt_mipmap_cache_one_t *)data;
  const uint32_t hash = key;
  const uint32_t slot = hash & c->cache->bucket_mask;
  *cost = c->buffer_size;
  return c->buf + slot * c->buffer_size;
}

void
dt_mipmap_cache_cleanup(void *data, const uint32_t key, void *payload)
{
  // nothing. memory is only allocated once.
}

void*
dt_mipmap_cache_allocate_dynamic(void *data, const uint32_t key, int32_t *cost)
{
  // for float preview and full image buffers
  dt_mipmap_cache_one_t *c     = (dt_mipmap_cache_one_t *)data;
  const uint32_t imgid         = get_imgid(key);
  const dt_mipmap_size_t size  = get_size(key);
  const dt_image_t *dt_image_cache_read_get(darktable.image_cache, (int32_t)imgid);
  const uint32_t bpp = img->bpp;
  const uint32_t buffer_size = (size == DT_MIPMAP_FULL) ?
                               (img->width * img->height * bpp) :
                               (DT_IMAGE_WIDOW_SIZE * DT_IMAGE_WINDOW_SIZE * 4*sizeof(float));
  dt_image_cache_read_release(darktable.image_cache, img);
  *cost = buffer_size;
  return dt_alloc_align(64, buffer_size);
}

void
dt_mipmap_cache_cleanup_dynamic(void *data, const uint32_t key, void *payload)
{
  free(payload);
}

void dt_mipmap_cache_init(dt_mipmap_cache_t *cache)
{
  // TODO: un-serialize!
  const int32_t max_th = 1000000, min_th = 20;
  int32_t thumbnails = dt_conf_get_int ("mipmap_cache_thumbnails");
  thumbnails = CLAMPS(thumbnails, min_th, max_th);
  const int32_t max_size = 2048, min_size = 32;
  int32_t wd = dt_conf_get_int ("plugins/lighttable/thumbnail_width");
  int32_t ht = dt_conf_get_int ("plugins/lighttable/thumbnail_height");
  wd = CLAMPS(wd, min_size, max_size);
  ht = CLAMPS(ht, min_size, max_size);
  // round up to a multiple of 8, so we can divide by two 3 times
  if(wd & 0xf) wd = (wd & ~0xf) + 0x10;
  if(ht & 0xf) ht = (ht & ~0xf) + 0x10;
  // cache these, can't change at runtime:
  cache->mip[DT_MIPMAP_4].max_width  = wd;
  cache->mip[DT_MIPMAP_4].max_height = ht;
  for(int k=DT_MIPMAP_3;k>=DT_MIPMAP_0;k--)
  {
    cache->mip[k].max_width  = cache->mip[k+1].max_width  / 2;
    cache->mip[k].max_height = cache->mip[k+1].max_height / 2;
  }

  // TODO:
    dt_print(DT_DEBUG_CACHE, "[mipmap_cache_init] cache has %d entries for mip %d.\n", entries, k);

  for(int k=0;k<DT_MIPMAP_F;k++)
  {
    dt_cache_init(&cache->mip[k].cache, thumbnails, 16, 64, 1);
    dt_cache_set_allocate_callback(&cache->mip[k].cache,
        &dt_mipmap_cache_allocate, &cache->mip[k].cache);
    dt_cache_set_cleanup_callback(&cache->mip[k].cache,
        &dt_mipmap_cache_cleanup, &cache->mip[k].cache);
    // buffer stores width and height + actual data
    cache->mip[k].buffer_size = (2 + width * height);
    cache->mip[k].size = k;
    cache->mip[k].buf = dt_alloc_align(64, thumbnails * cache->mip[k].buffer_size*sizeof(uint32_t));
    thumbnails >>= 2;
    thumbnails = CLAMPS(thumbnails, min_th, max_th);
  }
  // full buffer count for these:
  int32_t full_bufs = dt_conf_get_int ("mipmap_cache_full_images");
  dt_cache_init(&cache->mip[DT_MIPMAP_F].cache, full_bufs, 16, 64, 1);
  dt_cache_set_allocate_callback(&cache->mip[DT_MIPMAP_F].cache,
      &dt_mipmap_cache_allocate_dynamic, &cache->mip[DT_MIPMAP_F].cache);
  dt_cache_set_cleanup_callback(&cache->mip[DT_MIPMAP_F].cache,
      &dt_mipmap_cache_cleanup_dynamic, &cache->mip[DT_MIPMAP_F].cache);
  cache->mip[DT_MIPMAP_F].buffer_size = 0;
  cache->mip[DT_MIPMAP_F].buffer_cnt  = 0;
  cache->mip[DT_MIPMAP_F].size = DT_MIPMAP_F;
  cache->mip[DT_MIPMAP_F].buf = NULL;

  dt_cache_init(&cache->mip[DT_MIPMAP_FULL].cache, full_bufs, 16, 64, 1);
  dt_cache_set_allocate_callback(&cache->mip[DT_MIPMAP_FULL].cache,
      &dt_mipmap_cache_allocate_dynamic, &cache->mip[DT_MIPMAP_FULL].cache);
  dt_cache_set_cleanup_callback(&cache->mip[DT_MIPMAP_FULL].cache,
      &dt_mipmap_cache_cleanup_dynamic, &cache->mip[DT_MIPMAP_FULL].cache);
  cache->mip[DT_MIPMAP_FULL].buffer_size = 0;
  cache->mip[DT_MIPMAP_FULL].buffer_cnt  = 0;
  cache->mip[DT_MIPMAP_FULL].size = DT_MIPMAP_FULL;
  cache->mip[DT_MIPMAP_FULL].buf = NULL;
}

void dt_mipmap_cache_cleanup(dt_mipmap_cache_t *cache)
{
  // TODO: serialize
  for(int k=0;k<DT_MIPMAP_F;k++)
  {
    dt_cache_cleanup(&cache->mip[k].cache);
    // now mem is actually freed, not during cache cleanup
    free(cache->mip[k].buf);
  }
  dt_cache_cleanup(&cache->mip[DT_MIPMAP_F].cache);
  dt_cache_cleanup(&cache->mip[DT_MIPMAP_FULL].cache);
}

void dt_mipmap_cache_print(dt_mipmap_cache_t *cache)
{
  // TODO: get cache cost:
  int64_t buffers = 0;
  uint64_t bytes = 0;
  for(int k=0; k<(int)DT_IMAGE_NONE; k++)
  {
    int users = 0, write = 0, entries = 0;
    for(int i=0; i<cache->num_entries[k]; i++)
    {
      if(cache->mip_lru[k][i])
      {
        // dt_print(DT_DEBUG_CACHE, "[cache entry] buffer %d, image %s locks: %d r %d w\n", k, cache->mip_lru[k][i]->filename, cache->mip_lru[k][i]->lock[k].users, cache->mip_lru[k][i]->lock[k].write);
        entries++;
        users += cache->mip_lru[k][i]->lock[k].users;
        write += cache->mip_lru[k][i]->lock[k].write;
        bytes += cache->mip_lru[k][i]->mip_buf_size[k];
        if(cache->mip_lru[k][i]->mip_buf_size[k]) buffers ++;
#ifdef _DEBUG
        if(cache->mip_lru[k][i]->lock[k].users || cache->mip_lru[k][i]->lock[k].write)
          dt_print(DT_DEBUG_CACHE, "[mipmap_cache] img %d mip %d used by %d %s\n", cache->mip_lru[k][i]->id, k, cache->mip_lru[k][i]->lock[k].users, cache->mip_lru[k][i]->lock_last[k]);
#endif
      }
    }
    printf("[mipmap_cache] mip %d: fill: %d/%d, users: %d, writers: %d\n", k, entries, cache->num_entries[k], users, write);
    printf("[mipmap_cache] total memory in mip %d: %.2f MB\n", k, cache->total_size[k]/(1024.0*1024.0));
  }
  // printf("[mipmap_cache] occupies %.2f MB in %"PRIi64" (%"PRIi64") buffers\n", bytes/(1024.0*1024.0), buffers, dt_image_debug_malloc_size);
  printf("[mipmap_cache] occupies %.2f MB in %"PRIi64" (%.2f) buffers\n", bytes/(1024.0*1024.0), buffers, dt_image_debug_malloc_size/(1024.0*1024.0));
  
}

const dt_mipmap_buffer_t*
dt_mipmap_cache_read_get(
    dt_mipmap_cache_t *cache,
    const uint32_t key,
    dt_mipmap_size_t mip,
    dt_mipmap_get_flags_t flags)
{
#if 0 // and opposite: prefetch:
  if(!img || mip > DT_IMAGE_MIPF || mip < DT_IMAGE_MIP0) return;
  dt_pthread_mutex_lock(&(darktable.mipmap_cache->mutex));
  if(img->mip_buf_size[mip] > 0)
  {
    // already loaded.
    dt_pthread_mutex_unlock(&(darktable.mipmap_cache->mutex));
    return;
  }
  dt_job_t j;
  dt_image_load_job_init(&j, img->id, mip);
  // if the job already exists, make it high-priority, if not, add it:
  if(dt_control_revive_job(darktable.control, &j) < 0)
    dt_control_add_job(darktable.control, &j);
  dt_pthread_mutex_unlock(&(darktable.mipmap_cache->mutex));
#endif

  // TODO:
#if 0
// this should load and return with 'r' lock on mip buffer.
  if(!img) return 1;
  int ret = 0;
  char filename[DT_MAX_PATH];
  dt_image_full_path(img->id, filename, DT_MAX_PATH);
  // reimport forced?
  if(mip != DT_IMAGE_FULL &&
      (img->force_reimport || img->width == 0 || img->height == 0))
  {
    dt_image_reimport(img, filename, mip);
    if(dt_image_lock_if_available(img, mip, 'r')) ret = 1;
    else ret = 0;
  }
  // else we might be able to fetch it from the caches.
  else if(mip == DT_IMAGE_MIPF)
  {
    if(dt_image_lock_if_available(img, DT_IMAGE_FULL, 'r'))
    {
      // get mipf from half-size raw
      ret = dt_imageio_open_preview(img, filename);
      dt_image_validate(img, DT_IMAGE_MIPF);
      if(!ret && dt_image_lock_if_available(img, mip, 'r')) ret = 1;
      else ret = 0;
    }
    else
    {
      // downscale full buffer
      dt_image_raw_to_preview(img, img->pixels);
      dt_image_validate(img, DT_IMAGE_MIPF);
      dt_image_release(img, DT_IMAGE_FULL, 'r');
      if(dt_image_lock_if_available(img, mip, 'r')) ret = 1;
      else ret = 0;
    }
  }
  else if(mip == DT_IMAGE_FULL)
  {
    // after _open, the full buffer will be 'r' locked.
    ret = dt_imageio_open(img, filename);
    dt_image_raw_to_preview(img, img->pixels);
    dt_image_validate(img, DT_IMAGE_MIPF);
  }
  else
  {
    // refuse to load thumbnails for currently developed image.
    dt_ctl_gui_mode_t mode = dt_conf_get_int("ui_last/view");
    if(darktable.develop->image == img && mode == DT_DEVELOP) ret = 1;
    else
    {
      dt_image_reimport(img, filename, mip);
      if(dt_image_lock_if_available(img, mip, 'r')) ret = 1;
      else ret = 0;
    }
  }
  if(!ret) dt_image_validate(img, mip);

  return ret;
  
#endif
  // TODO: if _F or _FULL!

  // best-effort, might also return NULL.
  for(int k=mip;k>DT_MIPMAP_0;k--)
  {
    // TODO: query cache->cache[k]
    // TODO: 
  }
  return NULL;
}
dt_mipmap_buffer_t*
dt_mipmap_cache_write_get(
    dt_mipmap_cache_t *cache,
    const uint32_t key,
    dt_mipmap_size_t mip,
    dt_mipmap_get_flags_t flags)
{
  // TODO: 
}



// return the closest mipmap size
// for the given window you wish to draw.
// a dt_mipmap_size_t has always a fixed resolution associated with it,
// depending on the user parameter for the maximum thumbnail dimensions.
// actual resolution depends on the image and is only known after
// the thumbnail is loaded.
dt_mipmap_size_t
dt_mipmap_cache_get_matching_size(
    const dt_mipmap_cache_t *cache,
    const int32_t width,
    const int32_t height)
{
  uint32_t error = 0xffffffff;
  dt_mipmap_size_t best = DT_MIPMAP_NONE;
  for(int k=DT_MIPMAP_0;k<DT_MIPMAP_F;k++)
  {
    uint32_t new_error = abs(cache->mip[k].max_width + cache->mip[k].max_height
                       - width - height);
    if(new_error < error)
    {
      best = k;
      error = new_error;
    }
  }
  return best;
}


// *************************************************************
// TODO: some glue code down here. should be transparently
//       incorporated into the rest.
// *************************************************************


// TODO: rewrite with new interface, move to imageio?
// this is called by the innermost core functions when loading a raw preview
// because the full buffer needs to be freed right away!
// also if a full raw is loaded, the cache did it so far.
dt_imageio_retval_t dt_image_raw_to_preview(dt_image_t *img, const float *raw)
{
  const int raw_wd = img->width;
  const int raw_ht = img->height;
  int p_wd, p_ht;
  float f_wd, f_ht;
  dt_image_get_mip_size(img, DT_IMAGE_MIPF, &p_wd, &p_ht);
  dt_image_get_exact_mip_size(img, DT_IMAGE_MIPF, &f_wd, &f_ht);

  if(dt_image_alloc(img, DT_IMAGE_MIPF)) return DT_IMAGEIO_CACHE_FULL;
  dt_image_check_buffer(img, DT_IMAGE_MIPF, 4*p_wd*p_ht*sizeof(float));
  // memset(img->mipf, 0x0, sizeof(float)*4*p_wd*p_ht);

  dt_iop_roi_t roi_in, roi_out;
  roi_in.x = roi_in.y = 0;
  roi_in.width  = raw_wd;
  roi_in.height = raw_ht;
  roi_in.scale = 1.0f;
  roi_out.x = roi_out.y = 0;
  roi_out.width  = p_wd;//f_wd;
  roi_out.height = p_ht;//f_ht;
  roi_out.scale = fminf(f_wd/(float)raw_wd, f_ht/(float)raw_ht);
  if(img->filters)
  {
    // demosaic during downsample
    if(img->bpp == sizeof(float))
      dt_iop_clip_and_zoom_demosaic_half_size_f(img->mipf, (const float *)raw, &roi_out, &roi_in, p_wd, raw_wd, dt_image_flipped_filter(img));
    else
      dt_iop_clip_and_zoom_demosaic_half_size(img->mipf, (const uint16_t *)raw, &roi_out, &roi_in, p_wd, raw_wd, dt_image_flipped_filter(img));
  }
  else
  {
    // downsample
    dt_iop_clip_and_zoom(img->mipf, raw, &roi_out, &roi_in, p_wd, raw_wd);
  }

  dt_image_release(img, DT_IMAGE_MIPF, 'w');
  dt_image_release(img, DT_IMAGE_MIPF, 'r');
  return DT_IMAGEIO_OK;
}

// TODO: all the locking into the cache directly
// TOD: can the locking be done by finding the 'w' lock on the buffer?
int dt_image_import_testlock(dt_image_t *img)
{
  dt_pthread_mutex_lock(&darktable.db_insert);
  int lock = img->import_lock;
  if(!lock) img->import_lock = 1;
  dt_pthread_mutex_unlock(&darktable.db_insert);
  return lock;
}

void dt_image_import_unlock(dt_image_t *img)
{
  dt_pthread_mutex_lock(&darktable.db_insert);
  img->import_lock = 0;
  dt_pthread_mutex_unlock(&darktable.db_insert);
}

// TODO: get rid of it:
int dt_image_reimport(dt_image_t *img, const char *filename, dt_image_buffer_t mip)
{
  // TODO: first get the 'w' lock on the mip buffer/and test it
  if(dt_image_import_testlock(img))
  {
    // fprintf(stderr, "[image_reimport] someone is already loading `%s'!\n", filename);
    return 1;
  }
  if(!img->force_reimport)
  {
    dt_image_buffer_t mip1 = dt_image_get(img, mip, 'r');
    dt_image_release(img, mip1, 'r');
    if(mip1 == mip)
    {
      // already loaded
      dt_image_import_unlock(img);
      return 0;
    }
  }
  img->output_width = img->output_height = 0;
  dt_imageio_retval_t ret = dt_imageio_open_preview(img, filename);
  if(ret == DT_IMAGEIO_CACHE_FULL)
  {
    // handle resource conflicts if user provided very small caches:
    dt_image_import_unlock(img);
    return 1;
  }
  else if(ret != DT_IMAGEIO_OK)
  {
    // fprintf(stderr, "[image_reimport] could not open %s\n", filename);
    // dt_image_cleanup(img); // still locked buffers. cache will clean itself after a while.
    dt_control_log(_("image `%s' is not available"), img->filename);
    dt_image_import_unlock(img);
    // dt_image_remove(img->id);
    return 1;
  }

  // fprintf(stderr, "[image_reimport] loading `%s' to fill mip %d!\n", filename, mip);

  int altered = img->force_reimport;
  img->force_reimport = 0;
  if(dt_image_altered(img)) altered = 1;

  // open_preview actually only gave us a mipf and no mip4?
  if(!altered)
  {
    if(dt_image_lock_if_available(img, DT_IMAGE_MIP4, 'r'))
    {
      if(!dt_image_lock_if_available(img, DT_IMAGE_MIPF, 'r'))
      {
        // we have mipf but not mip4.
        altered = 1;
        dt_image_release(img, DT_IMAGE_MIPF, 'r');
      }
    }
    else dt_image_release(img, DT_IMAGE_MIP4, 'r');
  }

  if(altered)
  {
    dt_develop_t dev;
    dt_dev_init(&dev, 0);
    dt_dev_load_preview(&dev, img);
    dt_dev_process_to_mip(&dev);
    dt_dev_cleanup(&dev);
    // load preview keeps a lock on mipf:
    dt_image_release(img, DT_IMAGE_MIPF, 'r');
  }
  dt_image_import_unlock(img);
  return 0;
}

// TODO: this should never have to be called explicitly
dt_imageio_retval_t dt_image_update_mipmaps(dt_image_t *img)
{
  if(dt_image_lock_if_available(img, DT_IMAGE_MIP4, 'r')) return DT_IMAGEIO_CACHE_FULL;
  int oldwd, oldht;
  float fwd, fht;
  dt_image_get_mip_size(img, DT_IMAGE_MIP4, &oldwd, &oldht);
  dt_image_get_exact_mip_size(img, DT_IMAGE_MIP4, &fwd, &fht);
  img->mip_width  [DT_IMAGE_MIP4] = oldwd;
  img->mip_height  [DT_IMAGE_MIP4] = oldht;
  img->mip_width_f[DT_IMAGE_MIP4] = fwd;
  img->mip_height_f[DT_IMAGE_MIP4] = fht;

  // here we got mip4 'r' locked
  // create 8-bit mip maps:
  for(dt_image_buffer_t l=DT_IMAGE_MIP3; (int)l>=(int)DT_IMAGE_MIP0; l--)
  {
    // here we got mip l+1 'r' locked
    int p_wd, p_ht;
    dt_image_get_mip_size(img, l, &p_wd, &p_ht);
    dt_image_get_exact_mip_size(img, l, &fwd, &fht);
    if(dt_image_alloc(img, l))
    {
      dt_image_release(img, l+1, 'r');
      return DT_IMAGEIO_CACHE_FULL;
    }
    img->mip_width  [l] = p_wd;
    img->mip_height  [l] = p_ht;
    img->mip_width_f[l] = fwd;
    img->mip_height_f[l] = fht;

    // here, we got mip l+1 'r' locked, and  mip l 'rw'

    dt_image_check_buffer(img, l, p_wd*p_ht*4*sizeof(uint8_t));
    // printf("creating mipmap %d for img %s: %d x %d\n", l, img->filename, p_wd, p_ht);
    // downscale 8-bit mip
    if(oldwd != p_wd)
      for(int j=0; j<p_ht; j++) for(int i=0; i<p_wd; i++)
          for(int k=0; k<4; k++) img->mip[l][4*(j*p_wd + i) + k] = ((int)img->mip[l+1][8*(2*j)*p_wd + 4*(2*i) + k] + (int)img->mip[l+1][8*(2*j)*p_wd + 4*(2*i+1) + k]
                + (int)img->mip[l+1][8*(2*j+1)*p_wd + 4*(2*i+1) + k] + (int)img->mip[l+1][8*(2*j+1)*p_wd + 4*(2*i) + k])/4;
    else memcpy(img->mip[l], img->mip[l+1], 4*sizeof(uint8_t)*p_ht*p_wd);

    dt_image_release(img, l, 'w');
    dt_image_release(img, l+1, 'r');
    // here we got mip l 'r' locked
  }
  dt_image_release(img, DT_IMAGE_MIP0, 'r');
  return DT_IMAGEIO_OK;
}


