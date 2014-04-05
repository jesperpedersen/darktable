/*
    This file is part of darktable,
    copyright (c) 2009--2011 johannes hanika.

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
#include "common/image.h"
#include "common/image_cache.h"
#include "common/imageio_module.h"
#include "common/imageio.h"
#include "common/variables.h"
#include "common/metadata.h"
#include "common/debug.h"
#include "common/utility.h"
#include "common/imageio_storage.h"
#include "control/control.h"
#include "control/conf.h"
#include "gui/gtk.h"
#include "gui/gtkentry.h"
#include "dtgtk/button.h"
#include "dtgtk/paint.h"
#include <stdio.h>
#include <stdlib.h>

DT_MODULE(1)

// gui data
typedef struct gallery_t
{
  GtkEntry *entry;
  GtkEntry *title_entry;
}
gallery_t;

// saved params
typedef struct dt_imageio_gallery_t
{
  char filename[DT_MAX_PATH_LEN];
  char title[1024];
  char cached_dirname[DT_MAX_PATH_LEN]; // expanded during first img store, not stored in param struct.
  dt_variables_params_t *vp;
  GList *l;
}
dt_imageio_gallery_t;

// sorted list of all images
typedef struct pair_t
{
  char line[4096];
  int pos;
}
pair_t;


const char*
name (const struct dt_imageio_module_storage_t *self)
{
  return _("website gallery");
}

static void
button_clicked (GtkWidget *widget, dt_imageio_module_storage_t *self)
{
  gallery_t *d = (gallery_t *)self->gui_data;
  GtkWidget *win = dt_ui_main_window(darktable.gui->ui);
  GtkWidget *filechooser = gtk_file_chooser_dialog_new (_("select directory"),
                           GTK_WINDOW (win),
                           GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                           GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                           GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                           (char *)NULL);

  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(filechooser), FALSE);
  gchar *old = g_strdup(gtk_entry_get_text(d->entry));
  char *c = g_strstr_len(old, -1, "$");
  if(c) *c = '\0';
  gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(filechooser), old);
  g_free(old);
  if (gtk_dialog_run (GTK_DIALOG (filechooser)) == GTK_RESPONSE_ACCEPT)
  {
    gchar *dir = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (filechooser));
    char composed[DT_MAX_PATH_LEN];
    snprintf(composed, DT_MAX_PATH_LEN, "%s/$(FILE_NAME)", dir);
    gtk_entry_set_text(GTK_ENTRY(d->entry), composed);
    dt_conf_set_string("plugins/imageio/storage/gallery/file_directory", composed);
    g_free(dir);
  }
  gtk_widget_destroy (filechooser);
}

void
gui_init (dt_imageio_module_storage_t *self)
{
  gallery_t *d = (gallery_t *)malloc(sizeof(gallery_t));
  self->gui_data = (void *)d;
  self->widget = gtk_vbox_new(TRUE, 5);
  GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 0);
  GtkWidget *widget;

  widget = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(hbox), widget, TRUE, TRUE, 0);
  gchar *dir = dt_conf_get_string("plugins/imageio/storage/gallery/file_directory");
  if(dir)
  {
    gtk_entry_set_text(GTK_ENTRY(widget), dir);
    g_free(dir);
  }
  d->entry = GTK_ENTRY(widget);
  dt_gui_key_accel_block_on_focus_connect (GTK_WIDGET (d->entry));

  dt_gtkentry_completion_spec compl_list[] =
  {
    { "ROLL_NAME", _("$(ROLL_NAME) - roll of the input image") },
    { "FILE_FOLDER", _("$(FILE_FOLDER) - folder containing the input image") },
    { "FILE_NAME", _("$(FILE_NAME) - basename of the input image") },
    { "FILE_EXTENSION", _("$(FILE_EXTENSION) - extension of the input image") },
    { "SEQUENCE", _("$(SEQUENCE) - sequence number") },
    { "YEAR", _("$(YEAR) - year") },
    { "MONTH", _("$(MONTH) - month") },
    { "DAY", _("$(DAY) - day") },
    { "HOUR", _("$(HOUR) - hour") },
    { "MINUTE", _("$(MINUTE) - minute") },
    { "SECOND", _("$(SECOND) - second") },
    { "EXIF_YEAR", _("$(EXIF_YEAR) - EXIF year") },
    { "EXIF_MONTH", _("$(EXIF_MONTH) - EXIF month") },
    { "EXIF_DAY", _("$(EXIF_DAY) - EXIF day") },
    { "EXIF_HOUR", _("$(EXIF_HOUR) - EXIF hour") },
    { "EXIF_MINUTE", _("$(EXIF_MINUTE) - EXIF minute") },
    { "EXIF_SECOND", _("$(EXIF_SECOND) - EXIF second") },
    { "STARS", _("$(STARS) - star rating") },
    { "LABELS", _("$(LABELS) - colorlabels") },
    { "PICTURES_FOLDER", _("$(PICTURES_FOLDER) - pictures folder") },
    { "HOME", _("$(HOME) - home folder") },
    { "DESKTOP", _("$(DESKTOP) - desktop folder") },
    { NULL, NULL }
  };

  dt_gtkentry_setup_completion(GTK_ENTRY(widget), compl_list);

  char *tooltip_text = dt_gtkentry_build_completion_tooltip_text (
                         _("enter the path where to put exported images\nrecognized variables:"),
                         compl_list);
  g_object_set(G_OBJECT(widget), "tooltip-text", tooltip_text, (char *)NULL);
  g_free(tooltip_text);

  widget = dtgtk_button_new(dtgtk_cairo_paint_directory, 0);
  gtk_widget_set_size_request(widget, 18, 18);
  g_object_set(G_OBJECT(widget), "tooltip-text", _("select directory"), (char *)NULL);
  gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
  g_signal_connect(G_OBJECT(widget), "clicked", G_CALLBACK(button_clicked), self);

  hbox = gtk_hbox_new(FALSE, 5);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 0);
  widget = gtk_label_new(_("title"));
  gtk_misc_set_alignment(GTK_MISC(widget), 0.0f, 0.5f);
  gtk_box_pack_start(GTK_BOX(hbox), widget, FALSE, FALSE, 0);
  d->title_entry = GTK_ENTRY(gtk_entry_new());
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(d->title_entry), TRUE, TRUE, 0);
  dt_gui_key_accel_block_on_focus_connect (GTK_WIDGET (d->title_entry));
  g_object_set(G_OBJECT(d->title_entry), "tooltip-text", _("enter the title of the website"), (char *)NULL);
  dir = dt_conf_get_string("plugins/imageio/storage/gallery/title");
  if(dir)
  {
    gtk_entry_set_text(GTK_ENTRY(d->title_entry), dir);
    g_free(dir);
  }
}

void
gui_cleanup (dt_imageio_module_storage_t *self)
{
  gallery_t *d = (gallery_t *)self->gui_data;
  dt_gui_key_accel_block_on_focus_disconnect (GTK_WIDGET (d->entry));
  dt_gui_key_accel_block_on_focus_disconnect (GTK_WIDGET (d->title_entry));
  free(self->gui_data);
}

void
gui_reset (dt_imageio_module_storage_t *self)
{
  gallery_t *d = (gallery_t *)self->gui_data;
  dt_conf_set_string("plugins/imageio/storage/gallery/file_directory", gtk_entry_get_text(d->entry));
  dt_conf_set_string("plugins/imageio/storage/gallery/title", gtk_entry_get_text(d->title_entry));
}

static gint
sort_pos(pair_t *a, pair_t *b)
{
  return a->pos - b->pos;
}

int
store (dt_imageio_module_storage_t *self, dt_imageio_module_data_t *sdata, const int imgid, dt_imageio_module_format_t *format, dt_imageio_module_data_t *fdata,
       const int num, const int total, const gboolean high_quality)
{
  dt_imageio_gallery_t *d = (dt_imageio_gallery_t *)sdata;

  char filename[DT_MAX_PATH_LEN]= {0};
  char dirname[DT_MAX_PATH_LEN]= {0};
  gboolean from_cache = FALSE;
  dt_image_full_path(imgid, dirname, DT_MAX_PATH_LEN, &from_cache);
  // we're potentially called in parallel. have sequence number synchronized:
  dt_pthread_mutex_lock(&darktable.plugin_threadsafe);
  {

    char tmp_dir[DT_MAX_PATH_LEN];

    d->vp->filename = dirname;
    d->vp->jobcode = "export";
    d->vp->imgid = imgid;
    d->vp->sequence = num;
    dt_variables_expand(d->vp, d->filename, TRUE);
    g_strlcpy(tmp_dir, dt_variables_get_result(d->vp), DT_MAX_PATH_LEN);

    // if filenamepattern is a directory just let att ${FILE_NAME} as default..
    if ( g_file_test(tmp_dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR) || ((d->filename+strlen(d->filename)-1)[0]=='/' || (d->filename+strlen(d->filename)-1)[0]=='\\') )
      snprintf (d->filename+strlen(d->filename), DT_MAX_PATH_LEN-strlen(d->filename), "/$(FILE_NAME)");

    // avoid braindead export which is bound to overwrite at random:
    if(total > 1 && !g_strrstr(d->filename, "$"))
    {
      snprintf(d->filename+strlen(d->filename), DT_MAX_PATH_LEN-strlen(d->filename), "_$(SEQUENCE)");
    }

    gchar* fixed_path = dt_util_fix_path(d->filename);
    g_strlcpy(d->filename, fixed_path, DT_MAX_PATH_LEN);
    g_free(fixed_path);

    dt_variables_expand(d->vp, d->filename, TRUE);
    g_strlcpy(filename, dt_variables_get_result(d->vp), DT_MAX_PATH_LEN);
    g_strlcpy(dirname, filename, DT_MAX_PATH_LEN);

    const char *ext = format->extension(fdata);
    char *c = dirname + strlen(dirname);
    for(; c>dirname && *c != '/'; c--);
    if(*c == '/') *c = '\0';
    if(g_mkdir_with_parents(dirname, 0755))
    {
      fprintf(stderr, "[imageio_storage_gallery] could not create directory: `%s'!\n", dirname);
      dt_control_log(_("could not create directory `%s'!"), dirname);
      dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);
      return 1;
    }

    // store away dir.
    snprintf(d->cached_dirname, DT_MAX_PATH_LEN, "%s", dirname);

    c = filename + strlen(filename);
    for(; c>filename && *c != '.' && *c != '/' ; c--);
    if(c <= filename || *c=='/') c = filename + strlen(filename);

    sprintf(c,".%s",ext);

    // save image to list, in order:
    pair_t *pair = malloc(sizeof(pair_t));

    char *title = NULL, *description = NULL, *tags = NULL;
    GList *res_title, *res_desc, *res_subj;

    res_title = dt_metadata_get(imgid, "Xmp.dc.title", NULL);
    if(res_title)
    {
      title = res_title->data;
    }

    res_desc = dt_metadata_get(imgid, "Xmp.dc.description", NULL);
    if(res_desc)
    {
      description = res_desc->data;
    }

    unsigned int count = 0;
    res_subj = dt_metadata_get(imgid, "Xmp.dc.subject", &count);
    if(res_subj)
    {
      // don't show the internal tags (darktable|...)
      res_subj = g_list_first(res_subj);
      GList *iter = res_subj;
      while(iter)
      {
        GList *next = g_list_next(iter);
        if(g_str_has_prefix(iter->data, "darktable|"))
        {
          g_free(iter->data);
          res_subj = g_list_delete_link(res_subj, iter);
          count--;
        }
        iter = next;
      }
      tags = dt_util_glist_to_str(", ", res_subj, count);
    }

    char relfilename[256], relthumbfilename[256];
    c = filename + strlen(filename);
    for(; c>filename && *c != '/' ; c--);
    if(*c == '/') c++;
    if(c <= filename) c = filename;
    snprintf(relfilename, sizeof(relfilename), "%s", c);
    snprintf(relthumbfilename, sizeof(relthumbfilename), "%s", relfilename);
    c = relthumbfilename + strlen(relthumbfilename);
    for(; c>relthumbfilename && *c != '.'; c--);
    if(c <= relthumbfilename) c = relthumbfilename + strlen(relthumbfilename);
    sprintf(c, "-thumb.%s", ext);

    snprintf(pair->line, 4096,
             "            {image : \'%s\', title : \'%s\', thumb : \'%s\', url : \'%s\'},\n"
             , relfilename, title?title:relfilename, relthumbfilename, relfilename);

    pair->pos = num;

    if(title)
      g_free(title);

    if(description)
      g_free(description);

    if(tags)
      g_free(tags);

    pair->pos = num;
    if(res_title) g_list_free_full(res_title, &g_free);
    if(res_desc) g_list_free_full(res_desc, &g_free);
    if(res_subj) g_list_free_full(res_subj, &g_free);
    d->l = g_list_insert_sorted(d->l, pair, (GCompareFunc)sort_pos);
  } // end of critical block
  dt_pthread_mutex_unlock(&darktable.plugin_threadsafe);

  /* export image to file */
  if(dt_imageio_export(imgid, filename, format, fdata, high_quality,FALSE,self,sdata) != 0)
  {
    fprintf(stderr, "[imageio_storage_gallery] could not export to file: `%s'!\n", filename);
    dt_control_log(_("could not export to file `%s'!"), filename);
    return 1;
  }

  /* also export thumbnail - write with reduced resolution */
  const int max_width  = fdata->max_width;
  const int max_height = fdata->max_height;
  fdata->max_width  = 200;
  fdata->max_height = 200;
  // alter filename with -thumb:
  char *c = filename + strlen(filename);
  for(; c>filename && *c != '.' && *c != '/' ; c--);
  if(c <= filename || *c=='/') c = filename + strlen(filename);
  const char *ext = format->extension(fdata);
  sprintf(c,"-thumb.%s",ext);
  if(dt_imageio_export(imgid, filename, format, fdata, FALSE,FALSE,self,sdata) != 0)
  {
    fprintf(stderr, "[imageio_storage_gallery] could not export to file: `%s'!\n", filename);
    dt_control_log(_("could not export to file `%s'!"), filename);
    return 1;
  }
  // restore for next image:
  fdata->max_width = max_width;
  fdata->max_height = max_height;

  printf("[export_job] exported to `%s'\n", filename);
  char *trunc = filename + strlen(filename) - 32;
  if(trunc < filename) trunc = filename;
  dt_control_log(_("%d/%d exported to `%s%s'"), num, total, trunc != filename ? ".." : "", trunc);
  return 0;
}

static void
copy_res(const char *src, const char *dst)
{
  char share[DT_MAX_PATH_LEN];
  dt_loc_get_datadir(share, DT_MAX_PATH_LEN);
  gchar *sourcefile = g_build_filename(share, src, NULL);
  char* content = NULL;
  FILE *fin = fopen(sourcefile, "rb");
  FILE *fout = fopen(dst, "wb");

  if(fin && fout)
  {
    fseek(fin,0,SEEK_END);
    size_t end = ftell(fin);
    rewind(fin);
    content = (char*)g_malloc(sizeof(char)*end);
    if(content == NULL)
      goto END;
    if(fread(content,sizeof(char),end,fin) != end)
      goto END;
    if(fwrite(content,sizeof(char),end,fout) != end)
      goto END;
  }

END:
  if(fout != NULL)
    fclose(fout);
  if(fin != NULL)
    fclose(fin);

  g_free(content);
  g_free(sourcefile);
}

void
finalize_store(dt_imageio_module_storage_t *self, dt_imageio_module_data_t *dd)
{
  dt_imageio_gallery_t *d = (dt_imageio_gallery_t *)dd;
  char filename[DT_MAX_PATH_LEN];
  snprintf(filename, DT_MAX_PATH_LEN, "%s", d->cached_dirname);
  char *c = filename + strlen(filename);

  // create subdir style/ for CSS and related images
  sprintf(c, "/style");
  g_mkdir_with_parents(filename, 0755);

  sprintf(c, "/style/back.png");
  copy_res("/style/back.png", filename);
  sprintf(c, "/style/bg-black.png");
  copy_res("/style/bg-black.png", filename);
  sprintf(c, "/style/bg-hover.png");
  copy_res("/style/bg-hover.png", filename);
  sprintf(c, "/style/button-tray-down.png");
  copy_res("/style/button-tray-down.png", filename);
  sprintf(c, "/style/button-tray-up.png");
  copy_res("/style/button-tray-up.png", filename);
  sprintf(c, "/style/favicon.ico");
  copy_res("/style/favicon.ico", filename);
  sprintf(c, "/style/forward.png");
  copy_res("/style/forward.png", filename);
  sprintf(c, "/style/nav-bg.png");
  copy_res("/style/nav-bg.png", filename);
  sprintf(c, "/style/nav-dot.png");
  copy_res("/style/nav-dot.png", filename);
  sprintf(c, "/style/pause.png");
  copy_res("/style/pause.png", filename);
  sprintf(c, "/style/play.png");
  copy_res("/style/play.png", filename);
  sprintf(c, "/style/progress-back.png");
  copy_res("/style/progress-back.png", filename);
  sprintf(c, "/style/progress-bar.png");
  copy_res("/style/progress-bar.png", filename);
  sprintf(c, "/style/progress.gif");
  copy_res("/style/progress.gif", filename);
  sprintf(c, "/style/supersized.css");
  copy_res("/style/supersized.css", filename);
  sprintf(c, "/style/supersized.shutter.css");
  copy_res("/style/supersized.shutter.css", filename);
  sprintf(c, "/style/thumb-back.png");
  copy_res("/style/thumb-back.png", filename);
  sprintf(c, "/style/thumb-forward.png");
  copy_res("/style/thumb-forward.png", filename);

  // create subdir js for Java scripts
  sprintf(c, "/js");
  g_mkdir_with_parents(filename, 0755);

  sprintf(c, "/js/jquery.easing.min.js");
  copy_res("/js/jquery.easing.min.js", filename);
  sprintf(c, "/js/jquery.js");
  copy_res("/js/jquery.js", filename);
  sprintf(c, "/js/supersized.js");
  copy_res("/js/supersized.js", filename);
  sprintf(c, "/js/supersized.shutter.js");
  copy_res("/js/supersized.shutter.js", filename);

  sprintf(c, "/index.html");

  const char *title = d->title;

  FILE *f = fopen(filename, "wb");
  if(!f) return;
  fprintf(f,
          "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
          "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n"
          "  <head>\n"
          "    <title>%s</title>\n"
          "    <meta http-equiv=\"content-type\" content=\"text/html; charset=UTF-8\" />\n"
          "    <link rel=\"shortcut icon\" href=\"style/favicon.ico\"/>\n"
          "    <link rel=\"stylesheet\" href=\"style/supersized.css\" type=\"text/css\" media=\"screen\" />\n"
          "    <link rel=\"stylesheet\" href=\"style/supersized.shutter.css\" type=\"text/css\" media=\"screen\" />\n"
          "    <script type=\"text/javascript\" src=\"js/jquery.js\"></script>\n"
          "    <script type=\"text/javascript\" src=\"js/jquery.easing.min.js\"></script>\n"
          "    <script type=\"text/javascript\" src=\"js/supersized.js\"></script>\n"
          "    <script type=\"text/javascript\" src=\"js/supersized.shutter.js\"></script>\n"
          "    <script type=\"text/javascript\">\n"
          "      jQuery(function($){\n"
          "        $.supersized({\n"
          "          // Functionality\n"
          "          slideshow    : 1, // Slideshow on/off\n"
          "          autoplay     : 0, // Slideshow starts playing automatically\n"
          "          start_slide  : 1, // Start slide (0 is random)\n"
          "          stop_loop    : 0, // Pauses slideshow on last slide\n"
          "          random       : 0, // Randomize slide order (Ignores start slide)\n"
          "          slide_interval : 5000, // Length between transitions\n"
          "          transition     : 0, // 0-None, 1-Fade, 2-Slide Top, 3-Slide Right, 4-Slide Bottom, 5-Slide Left, 6-Carousel Right, 7-Carousel Left\n"
          "          transition_speed : 300, // Speed of transition\n"
          "          new_window  : 1, // Image links open in new window/tab\n"
          "          pause_hover : 0, // Pause slideshow on hover\n"
          "          keyboard_nav : 1, // Keyboard navigation on/off\n"
          "          performance  : 1, // 0-Normal, 1-Hybrid speed/quality, 2-Optimizes image quality, 3-Optimizes transition speed // (Only works for Firefox/IE, not Webkit)\n"
          "          image_protect : 1, // Disables image dragging and right click with Javascript\n"
          "\n"      
          "          // Size & Position\n"
          "          min_width         : 0, // Min width allowed (in pixels)\n"
          "          min_height        : 0, // Min height allowed (in pixels)\n"
          "          vertical_center   : 1, // Vertically center background\n"
          "          horizontal_center : 1, // Horizontally center background\n"
          "          fit_always        : 1, // Image will never exceed browser width or height (Ignores min. dimensions)\n"
          "          fit_portrait      : 1, // Portrait images will not exceed browser height\n"
          "          fit_landscape     : 1, // Landscape images will not exceed browser width\n"
          "\n"
          "          // Components\n"
          "          slide_links          : \'blank\', // Individual links for each slide (Options: false, \'num\', \'name\', \'blank\')\n"
          "          thumb_links          : 1, // Individual thumb links for each slide\n"
          "          thumbnail_navigation : 0, // Thumbnail navigation\n"
          "          slides : [ // Slideshow Images\n"
          , title?title:"");

  while(d->l)
  {
    pair_t *p = (pair_t *)d->l->data;
    fprintf(f, "%s", p->line);
    free(p);
    d->l = g_list_delete_link(d->l, d->l);
  }

  fprintf(f,
          "          ],\n"
          "\n"
          "          // Theme Options\n"
          "          progress_bar : 0, // Timer for each slide\n"
          "          mouse_scrub  : 0\n"
          "        });\n"
          "      });\n"
          "    </script>\n"
          "  </head>\n"
          "  <body>\n"
          "    <!--Thumbnail Navigation-->\n"
          "    <div id=\"prevthumb\"></div>\n"
          "    <div id=\"nextthumb\"></div>\n"
          "\n"
          "    <!--Arrow Navigation-->\n"
          "    <a id=\"prevslide\" class=\"load-item\"></a>\n"
          "    <a id=\"nextslide\" class=\"load-item\"></a>\n"
          "\n"
          "    <div id=\"thumb-tray\" class=\"load-item\">\n"
          "      <div id=\"thumb-back\"></div>\n"
          "      <div id=\"thumb-forward\"></div>\n"
          "    </div>\n"
          "\n"
          "    <!--Time Bar\n"
          "    <div id=\"progress-back\" class=\"load-item\">\n"
          "      <div id=\"progress-bar\"></div>\n"
          "    </div>\n"
          "    -->\n"
          "\n"
          "    <!--Control Bar-->\n"
          "    <div id=\"controls-wrapper\" class=\"load-item\">\n"
          "      <div id=\"controls\">\n"
          "        <a id=\"play-button\"><img id=\"pauseplay\" src=\"style/pause.png\"/></a>\n"
          "\n"
          "        <div id=\"slidecounter\">\n"
          "          <span class=\"slidenumber\"></span> / <span class=\"totalslides\"></span>\n"
          "        </div>\n"
          "\n"        
          "        <div id=\"slidecaption\"></div>\n"
          "          <a id=\"tray-button\"><img id=\"tray-arrow\" src=\"style/button-tray-up.png\"/></a>\n"
          "          <ul id=\"slide-list\"></ul>\n"
          "        </div>\n"
          "      </div>\n"
          "   </body>\n"
          "</html>\n"
         );
  fclose(f);
}

size_t
params_size(dt_imageio_module_storage_t *self)
{
  return sizeof(dt_imageio_gallery_t) - 2*sizeof(void *) - 1024;
}

void init(dt_imageio_module_storage_t *self)
{
#ifdef USE_LUA
  dt_lua_register_module_member(darktable.lua_state.state,self,dt_imageio_gallery_t,filename,char_path_length);
  dt_lua_register_module_member(darktable.lua_state.state,self,dt_imageio_gallery_t,title,char_1024);
#endif
}

void*
get_params(dt_imageio_module_storage_t *self)
{
  dt_imageio_gallery_t *d = (dt_imageio_gallery_t *)malloc(sizeof(dt_imageio_gallery_t));
  memset(d, 0, sizeof(dt_imageio_gallery_t));
  gallery_t *g = (gallery_t *)self->gui_data;
  d->vp = NULL;
  d->l = NULL;
  dt_variables_params_init(&d->vp);
  const char *text = gtk_entry_get_text(GTK_ENTRY(g->entry));
  g_strlcpy(d->filename, text, DT_MAX_PATH_LEN);
  dt_conf_set_string("plugins/imageio/storage/gallery/file_directory", d->filename);
  text = gtk_entry_get_text(GTK_ENTRY(g->title_entry));
  g_strlcpy(d->title, text, DT_MAX_PATH_LEN);
  dt_conf_set_string("plugins/imageio/storage/gallery/title", d->title);
  return d;
}

void
free_params(dt_imageio_module_storage_t *self, dt_imageio_module_data_t *params)
{
  dt_imageio_gallery_t *d = (dt_imageio_gallery_t *)params;
  dt_variables_params_destroy(d->vp);
  free(params);
}

int
set_params(dt_imageio_module_storage_t *self, const void *params, const int size)
{
  if(size != self->params_size(self)) return 1;
  dt_imageio_gallery_t *d = (dt_imageio_gallery_t *)params;
  gallery_t *g = (gallery_t *)self->gui_data;
  gtk_entry_set_text(GTK_ENTRY(g->entry), d->filename);
  dt_conf_set_string("plugins/imageio/storage/gallery/file_directory", d->filename);
  gtk_entry_set_text(GTK_ENTRY(g->title_entry), d->title);
  dt_conf_set_string("plugins/imageio/storage/gallery/title", d->title);
  return 0;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
