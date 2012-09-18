/*
    This file is part of darktable,
    copyright (c) 2012 Ulrich Pegelow.

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

#ifndef DT_COMMON_GAUSSIAN_H
#define DT_COMMON_GAUSSIAN_H

#include <math.h>
#include <assert.h>
#include <xmmintrin.h>

#define CLAMPF(a, mn, mx) ((a) < (mn) ? (mn) : ((a) > (mx) ? (mx) : (a)))
#define MMCLAMPPS(a, mn, mx) (_mm_min_ps((mx), _mm_max_ps((a), (mn))))

#ifndef DT_GAUSSIAN_ORDER_T
#define DT_GAUSSIAN_ORDER_T
typedef enum dt_gaussian_order_t
{
  DT_IOP_GAUSSIAN_ZERO = 0,
  DT_IOP_GAUSSIAN_ONE = 1,
  DT_IOP_GAUSSIAN_TWO = 2
}
dt_gaussian_order_t;
#endif


typedef struct dt_gaussian_t
{
  int width, height, channels;
  float sigma;
  int order;
  float *max;
  float *min;
  float *buf;
}
dt_gaussian_t;


static 
void compute_gauss_params(const float sigma, dt_gaussian_order_t order, float *a0, float *a1, float *a2, float *a3, 
                          float *b1, float *b2, float *coefp, float *coefn)
{
  const float alpha = 1.695f / sigma;
  const float ema = exp(-alpha);
  const float ema2 = exp(-2.0f * alpha);
  *b1 = -2.0f * ema;
  *b2 = ema2;
  *a0 = 0.0f;
  *a1 = 0.0f;
  *a2 = 0.0f;
  *a3 = 0.0f;
  *coefp = 0.0f;
  *coefn = 0.0f;

  switch(order)
  {
    default:
    case DT_IOP_GAUSSIAN_ZERO:
    {
      const float k = (1.0f - ema)*(1.0f - ema)/(1.0f + (2.0f * alpha * ema) - ema2);
      *a0 = k;
      *a1 = k * (alpha - 1.0f) * ema;
      *a2 = k * (alpha + 1.0f) * ema;
      *a3 = -k * ema2;
    }
    break;

    case DT_IOP_GAUSSIAN_ONE:
    {
      *a0 = (1.0f - ema)*(1.0f - ema);
      *a1 = 0.0f;
      *a2 = -*a0;
      *a3 = 0.0f;
    }
    break;

    case DT_IOP_GAUSSIAN_TWO:
    {
      const float k = -(ema2 - 1.0f) / (2.0f * alpha * ema);
      float kn = -2.0f * (-1.0f + (3.0f * ema) - (3.0f * ema * ema) + (ema * ema * ema));
      kn /= ((3.0f * ema) + 1.0f + (3.0f * ema * ema) + (ema * ema * ema));
      *a0 = kn;
      *a1 = -kn * (1.0f + (k * alpha)) * ema;
      *a2 = kn * (1.0f - (k * alpha)) * ema;
      *a3 = -kn * ema2;
    }
  }

  *coefp = (*a0 + *a1)/(1.0f + *b1 + *b2);
  *coefn = (*a2 + *a3)/(1.0f + *b1 + *b2);
}


dt_gaussian_t *
dt_gaussian_init(
    const int width,       // width of input image
    const int height,      // height of input image
    const int channels,    // channels per pixel
    const float *max,      // maximum allowed values per channel for clamping
    const float *min,      // minimum allowed values per channel for clamping
    const float sigma,     // gaussian sigma
    const int order)       // order of gaussian blur
{
  dt_gaussian_t *g = (dt_gaussian_t *)malloc(sizeof(dt_gaussian_t));
  if(!g) return NULL;

  g->width = width;
  g->height = height;
  g->channels = channels;
  g->sigma = sigma;
  g->order = order;
  g->buf = NULL;
  g->max = (float *)malloc(channels * sizeof(float));
  g->min = (float *)malloc(channels * sizeof(float));

  if(!g->min || !g->max) goto error;

  for(int k=0; k < channels; k++)
  {
    g->max[k] = max[k];
    g->min[k] = min[k];
  }

  g->buf = dt_alloc_align(64, width*height*channels*sizeof(float));
  if(!g->buf) goto error;

  return g;

error:
  free(g->buf);
  free(g->max);
  free(g->min);
  free(g);
  return NULL;
}


void
dt_gaussian_blur(
    dt_gaussian_t *g,
    float    *in,
    float    *out)
{

  const int width = g->width;
  const int height = g->height;
  const int ch = g->channels;

  float a0, a1, a2, a3, b1, b2, coefp, coefn;

  compute_gauss_params(g->sigma, g->order, &a0, &a1, &a2, &a3, &b1, &b2, &coefp, &coefn);

  float *temp = g->buf;

  float *Labmax = g->max;
  float *Labmin = g->min;

  // vertical blur column by column
#ifdef _OPENMP
  #pragma omp parallel for default(none) shared(in,out,temp,Labmin,Labmax,a0,a1,a2,a3,b1,b2,coefp,coefn) schedule(static)
#endif
  for(int i=0; i<width; i++)
  {
    float xp[ch];
    float yb[ch];
    float yp[ch];
    float xc[ch];
    float yc[ch];
    float xn[ch];
    float xa[ch];
    float yn[ch];
    float ya[ch];

    // forward filter
    for(int k=0; k<ch; k++)
    {
      xp[k] = CLAMPF(in[i*ch+k], Labmin[k], Labmax[k]);
      yb[k] = xp[k] * coefp;
      yp[k] = yb[k];
      xc[k] = yc[k] = xn[k] = xa[k] = yn[k] = ya[k] = 0.0f;
    }
 
    for(int j=0; j<height; j++)
    {
      int offset = (i + j * width)*ch;

      for(int k=0; k<ch; k++)
      {
        xc[k] = CLAMPF(in[offset+k], Labmin[k], Labmax[k]);
        yc[k] = (a0 * xc[k]) + (a1 * xp[k]) - (b1 * yp[k]) - (b2 * yb[k]);

        temp[offset+k] = yc[k];

        xp[k] = xc[k];
        yb[k] = yp[k];
        yp[k] = yc[k];
      }
    }

    // backward filter
    for(int k=0; k<ch; k++)
    {
      xn[k] = CLAMPF(in[((height - 1) * width + i)*ch+k], Labmin[k], Labmax[k]);
      xa[k] = xn[k];
      yn[k] = xn[k] * coefn;
      ya[k] = yn[k];
    }

    for(int j=height - 1; j > -1; j--)
    {
      int offset = (i + j * width)*ch;

      for(int k=0; k<ch; k++)
      {      
        xc[k] = CLAMPF(in[offset+k], Labmin[k], Labmax[k]);

        yc[k] = (a2 * xn[k]) + (a3 * xa[k]) - (b1 * yn[k]) - (b2 * ya[k]);

        xa[k] = xn[k]; 
        xn[k] = xc[k]; 
        ya[k] = yn[k]; 
        yn[k] = yc[k];

        temp[offset+k] += yc[k];
      }
    }
  }

  // horizontal blur line by line
#ifdef _OPENMP
  #pragma omp parallel for default(none) shared(out,temp,Labmin,Labmax,a0,a1,a2,a3,b1,b2,coefp,coefn) schedule(static)
#endif
  for(int j=0; j<height; j++)
  {
    float xp[ch];
    float yb[ch];
    float yp[ch];
    float xc[ch];
    float yc[ch];
    float xn[ch];
    float xa[ch];
    float yn[ch];
    float ya[ch];

    // forward filter
    for(int k=0; k<ch; k++)
    {
      xp[k] = CLAMPF(temp[j*width*ch+k], Labmin[k], Labmax[k]);
      yb[k] = xp[k] * coefp;
      yp[k] = yb[k];
      xc[k] = yc[k] = xn[k] = xa[k] = yn[k] = ya[k] = 0.0f;
    }
 
    for(int i=0; i<width; i++)
    {
      int offset = (i + j * width)*ch;

      for(int k=0; k<ch; k++)
      {
        xc[k] = CLAMPF(temp[offset+k], Labmin[k], Labmax[k]);
        yc[k] = (a0 * xc[k]) + (a1 * xp[k]) - (b1 * yp[k]) - (b2 * yb[k]);

        out[offset+k] = yc[k];

        xp[k] = xc[k];
        yb[k] = yp[k];
        yp[k] = yc[k];
      }
    }

    // backward filter
    for(int k=0; k<ch; k++)
    {
      xn[k] = CLAMPF(temp[((j + 1)*width - 1)*ch + k], Labmin[k], Labmax[k]);
      xa[k] = xn[k];
      yn[k] = xn[k] * coefn;
      ya[k] = yn[k];
    }

    for(int i=width - 1; i > -1; i--)
    {
      int offset = (i + j * width)*ch;

      for(int k=0; k<ch; k++)
      {      
        xc[k] = CLAMPF(temp[offset+k], Labmin[k], Labmax[k]);

        yc[k] = (a2 * xn[k]) + (a3 * xa[k]) - (b1 * yn[k]) - (b2 * ya[k]);

        xa[k] = xn[k]; 
        xn[k] = xc[k]; 
        ya[k] = yn[k]; 
        yn[k] = yc[k];

        out[offset+k] += yc[k];
      }
    }
  }
}



void
dt_gaussian_blur_4c(
    dt_gaussian_t *g,
    float    *in,
    float    *out)
{

  const int width = g->width;
  const int height = g->height;
  const int ch = 4;

  assert(g->channels == 4);

  float a0, a1, a2, a3, b1, b2, coefp, coefn;

  compute_gauss_params(g->sigma, g->order, &a0, &a1, &a2, &a3, &b1, &b2, &coefp, &coefn);

  const __m128 Labmax = _mm_set_ps(g->max[3], g->max[2], g->max[1], g->max[0]);
  const __m128 Labmin = _mm_set_ps(g->min[3], g->min[2], g->min[1], g->min[0]);

  float *temp = g->buf;


  // vertical blur column by column
#ifdef _OPENMP
  #pragma omp parallel for default(none) shared(in,out,temp,a0,a1,a2,a3,b1,b2,coefp,coefn) schedule(static)
#endif
  for(int i=0; i<width; i++)
  {
    __m128 xp = _mm_setzero_ps();
    __m128 yb = _mm_setzero_ps();
    __m128 yp = _mm_setzero_ps();
    __m128 xc = _mm_setzero_ps();
    __m128 yc = _mm_setzero_ps();
    __m128 xn = _mm_setzero_ps();
    __m128 xa = _mm_setzero_ps();
    __m128 yn = _mm_setzero_ps();
    __m128 ya = _mm_setzero_ps();

    // forward filter
    xp = MMCLAMPPS(_mm_load_ps(in+i*ch), Labmin, Labmax);
    yb = _mm_mul_ps(_mm_set_ps1(coefp), xp);
    yp = yb;

 
    for(int j=0; j<height; j++)
    {
      int offset = (i + j * width)*ch;

      xc = MMCLAMPPS(_mm_load_ps(in+offset), Labmin, Labmax);


      yc = _mm_add_ps(_mm_mul_ps(xc, _mm_set_ps1(a0)),
           _mm_sub_ps(_mm_mul_ps(xp, _mm_set_ps1(a1)),
           _mm_add_ps(_mm_mul_ps(yp, _mm_set_ps1(b1)), _mm_mul_ps(yb, _mm_set_ps1(b2)))));

      _mm_store_ps(temp+offset, yc);

      xp = xc;
      yb = yp;
      yp = yc;

    }

    // backward filter
    xn = MMCLAMPPS(_mm_load_ps(in+((height - 1) * width + i)*ch), Labmin, Labmax);
    xa = xn;
    yn = _mm_mul_ps(_mm_set_ps1(coefn), xn);
    ya = yn;

    for(int j=height - 1; j > -1; j--)
    {
      int offset = (i + j * width)*ch;

      xc = MMCLAMPPS(_mm_load_ps(in+offset), Labmin, Labmax);

      yc = _mm_add_ps(_mm_mul_ps(xn, _mm_set_ps1(a2)),
           _mm_sub_ps(_mm_mul_ps(xa, _mm_set_ps1(a3)),
           _mm_add_ps(_mm_mul_ps(yn, _mm_set_ps1(b1)), _mm_mul_ps(ya, _mm_set_ps1(b2)))));


      xa = xn; 
      xn = xc; 
      ya = yn; 
      yn = yc;

      _mm_store_ps(temp+offset, _mm_add_ps(_mm_load_ps(temp+offset), yc));
    }
  }

  // horizontal blur line by line
#ifdef _OPENMP
  #pragma omp parallel for default(none) shared(out,temp,a0,a1,a2,a3,b1,b2,coefp,coefn) schedule(static)
#endif
  for(int j=0; j<height; j++)
  {
    __m128 xp = _mm_setzero_ps();
    __m128 yb = _mm_setzero_ps();
    __m128 yp = _mm_setzero_ps();
    __m128 xc = _mm_setzero_ps();
    __m128 yc = _mm_setzero_ps();
    __m128 xn = _mm_setzero_ps();
    __m128 xa = _mm_setzero_ps();
    __m128 yn = _mm_setzero_ps();
    __m128 ya = _mm_setzero_ps();

    // forward filter
    xp = MMCLAMPPS(_mm_load_ps(temp+j*width*ch), Labmin, Labmax);
    yb = _mm_mul_ps(_mm_set_ps1(coefp), xp);
    yp = yb;

 
    for(int i=0; i<width; i++)
    {
      int offset = (i + j * width)*ch;

      xc = MMCLAMPPS(_mm_load_ps(temp+offset), Labmin, Labmax);

      yc = _mm_add_ps(_mm_mul_ps(xc, _mm_set_ps1(a0)),
           _mm_sub_ps(_mm_mul_ps(xp, _mm_set_ps1(a1)),
           _mm_add_ps(_mm_mul_ps(yp, _mm_set_ps1(b1)), _mm_mul_ps(yb, _mm_set_ps1(b2)))));

      _mm_store_ps(out+offset, yc);

      xp = xc;
      yb = yp;
      yp = yc;
    }

    // backward filter
    xn = MMCLAMPPS(_mm_load_ps(temp+((j + 1)*width - 1)*ch), Labmin, Labmax);
    xa = xn;
    yn = _mm_mul_ps(_mm_set_ps1(coefn), xn);
    ya = yn;


    for(int i=width - 1; i > -1; i--)
    {
      int offset = (i + j * width)*ch;

      xc = MMCLAMPPS(_mm_load_ps(temp+offset), Labmin, Labmax);

      yc = _mm_add_ps(_mm_mul_ps(xn, _mm_set_ps1(a2)),
           _mm_sub_ps(_mm_mul_ps(xa, _mm_set_ps1(a3)),
           _mm_add_ps(_mm_mul_ps(yn, _mm_set_ps1(b1)), _mm_mul_ps(ya, _mm_set_ps1(b2)))));


      xa = xn; 
      xn = xc; 
      ya = yn; 
      yn = yc;

      _mm_store_ps(out+offset, _mm_add_ps(_mm_load_ps(out+offset), yc));
    }
  }
}


void
dt_gaussian_free(
    dt_gaussian_t *g)
{
  if(!g) return;
  free(g->buf);
  free(g->min);
  free(g->max);
  free(g);
}

#endif