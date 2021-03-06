                                                                            /*
Kernagic a libre auto spacer and kerner for Unified Font Objects.
Copyright (C) 2013 Øyvind Kolås

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.       */

#include <dirent.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <assert.h>
#include <math.h>
#include "kernagic.h"

#define SPECIMEN_SIZE 170

KernerSettings kerner_settings = {
  0,
  KERNER_DEFAULT_MIN,
  KERNER_DEFAULT_MAX,
  KERNER_DEFAULT_DIVISOR,
  KERNER_DEFAULT_TARGET_GRAY,
  KERNER_DEFAULT_OFFSET,
  KERNER_DEFAULT_TRACKING
};



char *kernagic_sample_text = NULL;

static char *loaded_ufo_path = NULL;
static GList *glyphs = NULL;
float  scale_factor = 0.18;
static gunichar *glyph_string = NULL;

extern KernagicMethod *kernagic_cadence,
   //                   *kernagic_rythm,
                      *kernagic_gap,
                      *kernagic_gray,
                      *kernagic_original,
                      *kernagic_bounds;

KernagicMethod *methods[32] = {NULL};

static void init_methods (void)
{
  int i = 0;
  methods[i++] = kernagic_original;
  methods[i++] = kernagic_bounds;
  //methods[i++] = kernagic_gray;
  methods[i++] = kernagic_cadence;
  //methods[i++] = kernagic_rythm;
  methods[i++] = kernagic_gap;
  methods[i] = NULL;
};


gboolean kernagic_strip_bearing = FALSE;

GList *kernagic_glyphs (void)
{
  return glyphs;
}

void init_kernagic (void)
{
  init_kerner ();
}

static gint unicode_sort (gconstpointer a, gconstpointer b)
{
  const Glyph *ga = a;
  const Glyph *gb = b;

  return ga->unicode - gb->unicode;
}

static int add_glyph(const char *fpath)
{
  if (strstr (fpath, "contents.plist"))
    return 0;
  Glyph *glyph = kernagic_glyph_new (fpath);

  if (glyph)
    glyphs = g_list_insert_sorted (glyphs, glyph, unicode_sort);
    //glyphs = g_list_prepend (glyphs, glyph);
  return 0;
}

/* simplistic recomputation of right bearings; it uses the average advance
 * taking all kerning pairs into account, and modifies all the involved
 * kerning pairs accordingly.
 */
void
recompute_right_bearings ()
{
  GList *l;
  for (l = glyphs; l; l= l->next)
    {
      Glyph *lg = l->data;
      GList *r;
      float advance_sum = 0;
      long  glyph_count = 0;
      int   new_advance;
      int   advance_diff;
      for (r = glyphs; r; r= r->next)
        {
          Glyph *rg = r->data;
          advance_sum += kernagic_kern_get (lg, rg) + kernagic_get_advance (lg);
          glyph_count ++;
        }
      new_advance = advance_sum / glyph_count;
      advance_diff = new_advance - kernagic_get_advance (lg);

      lg->left_bearing  = 0;
      lg->right_bearing = new_advance;

      for (r = glyphs; r; r= r->next)
        {
          Glyph *rg = r->data;
          float oldkern = kernagic_kern_get (lg, rg);
          kernagic_set_kerning (lg, rg, oldkern - advance_diff);
        }
    }
}

void kernagic_save_kerning_info (void)
{
  GString *str = g_string_new (
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\"\n"
"\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
"<plist version=\"1.0\">\n"
"  <dict>\n");

  GList *left, *right;

  for (left = glyphs; left; left = left->next)
  {
    Glyph *lg = left->data;
    int found = 0;

    for (right = glyphs; right; right = right->next)
      {
        Glyph *rg = right->data;
        float kerning;

        if ((kerning=kernagic_kern_get (lg, rg)) != 0.0)
          {
            if (!found)
              {
    g_string_append_printf (str, "    <key>%s</key>\n    <dict>\n", lg->name);
    found = 1;
              }
      
            g_string_append_printf (str, "      <key>%s</key><integer>%d</integer>\n", rg->name, (int)kerning);

          }
      }
    if (found)
      g_string_append (str, "    </dict>\n");
  }

  g_string_append (str, "  </dict>\n</plist>");

  char path[4095];
  GError *error = NULL;

  sprintf (path, "%s/kerning.plist", loaded_ufo_path);

  g_file_set_contents (path, str->str, -1, &error);
  if (error)
    fprintf (stderr, "EEeek %s\n", error->message);
  g_string_free (str, TRUE);

  GList *l;
  for (l = glyphs; l; l = l->next)
    {
      Glyph *glyph = l->data;
      rewrite_ufo_glyph (glyph);
    }
}

void render_ufo_glyph (Glyph *glyph);

void kernagic_load_ufo (const char *font_path, gboolean strip_left_bearing)
{
  char path[4095];

  kernagic_strip_bearing = strip_left_bearing;

  if (loaded_ufo_path)
    g_free (loaded_ufo_path);
  loaded_ufo_path = g_strdup (font_path);
  if (loaded_ufo_path [strlen(loaded_ufo_path)] == '/')
    loaded_ufo_path [strlen(loaded_ufo_path)] = '\0';

  GList *l;
  for (l = glyphs; l; l = l->next)
    {
      Glyph *glyph = l->data;
      kernagic_glyph_free (glyph);
    }
  g_list_free (glyphs);
  glyphs = NULL;

  sprintf (path, "%s/glyphs", loaded_ufo_path);

  {
    DIR *dp;
    struct dirent *dirp;
    dp = opendir(path);

    if (dp)
      {
      while ((dirp = readdir(dp)) != NULL) {
        if (dirp->d_name[0] != '.')
          {
            char buf[1024];
            sprintf (buf, "%s/%s", path, dirp->d_name);
            add_glyph (buf);
          }
      }
      closedir(dp);
    }
    else
    {
      return ;
    }
  }

  scale_factor = SPECIMEN_SIZE / kernagic_x_height ();

  for (l = glyphs; l; l = l->next)
    {
      Glyph *glyph = l->data;
      int y;
      render_ufo_glyph (glyph);

      for (y = 0; y < glyph->r_height; y ++)
      {
        int x;
        int min = 8192;
        int max = -1;
        for (x = 0; x < glyph->r_width; x ++)
          {
            if (glyph->raster[y * glyph->r_width + x] > 0)
              {
                if (x < min)
                  min = x;
                if (x > max)
                  max = x;
              }
          }
        glyph->leftmost[y] = min;
        glyph->rightmost[y] = max;
      }
    }
  init_kernagic ();
}

void kernagic_kern_clear_all (void)
{
  GList *l;
  for (l = glyphs; l; l = l->next)
    {
      Glyph *glyph = l->data;
      g_hash_table_remove_all (glyph->kerning);
    }
}

Glyph *kernagic_find_glyph (const char *name)
{
  GList *l;
  for (l = glyphs; l; l = l->next)
    {
      Glyph *glyph = l->data;
      if (glyph->name && !strcmp (glyph->name, name))
        return glyph;
    }
  return NULL;
}

Glyph *kernagic_find_glyph_unicode (unsigned int unicode)
{
  GList *l;
  for (l = glyphs; l; l = l->next)
    {
      Glyph *glyph = l->data;
      if (glyph->unicode == unicode)
        return glyph;
    }
  return NULL;
}

/* programmatic way of finding x-height, is guaranteed to work better than
 * font metadata...
 */
float kernagic_x_height (void)
{
  Glyph *g = kernagic_find_glyph_unicode ('x');
  if (!g)
    return 1.0; /* avoiding 0, for divisions by it.. */
  return (g->ink_max_y - g->ink_min_y);
}

gboolean kernagic_deal_with_glyph (gunichar unicode)
{
  int i;
  if (!glyph_string)
    return TRUE;
  for (i = 0; glyph_string[i]; i++)
    if (glyph_string[i] == unicode)
      return TRUE;
  return FALSE;
}

gboolean kernagic_deal_with_glyphs (gunichar unicode, gunichar unicode2)
{
  int i;
  if (!glyph_string)
    return TRUE;
  if (!unicode || !unicode2)
    return FALSE;
  for (i = 0; glyph_string[i]; i++)
    if (glyph_string[i] == unicode &&
        glyph_string[i+1] == unicode2)
      return TRUE;
  return FALSE;
}

void   kernagic_set_glyph_string (const char *utf8)
{
  if (glyph_string)
    g_free (glyph_string);
  glyph_string = NULL;
  if (utf8)
    glyph_string = g_utf8_to_ucs4 (utf8, -1, NULL, NULL, NULL);
}

static int interactive = 1;

gboolean kernagic_strip_left_bearing = KERNAGIC_DEFAULT_STRIP_LEFT_BEARING;
char *kernagic_output = NULL;
char *kernagic_sample_string = NULL;
char *kernagic_output_png = NULL;

void help (void)
{
  printf ("kernagic [options] <font.ufo>\n"
          "\n"
          "Options:\n"
          "   -m <method>   specify method, one of gray, period and rythmic "
          "   suboptions influencing gap method:\n"
          "       -p   period of rythm, in fonts units\n"
          "       -o   offset, in number of periods from edge of glyph to stem\n"
          "\n"
          "   -O <output.ufo>  create a copy of the input font, this make kernagic run non-interactive\n"
          "   -P <output.png>  write the test string to a png, using options\n"
          "   -T <string>      sample string\n"
          "\n");
  exit (0);
}

const char *ufo_path = NULL;
int ipsumat (int argc, char **argv);

void parse_args (int argc, char **argv)
{
  int no;
  kerner_settings.method = methods[3];

  for (no = 1; no < argc; no++)
    {
      if (!strcmp (argv[no], "--help") ||
          !strcmp (argv[no], "-h"))
        help ();
      else if (!strcmp (argv[no], "--ipsumat"))
        {
          exit (ipsumat (argc, argv));
        }
      else if (!strcmp (argv[no], "-d"))
        {
#define EXPECT_ARG if (!argv[no+1]) {fprintf (stderr, "expected argument after %s\n", argv[no]);exit(-1);}

          EXPECT_ARG;
          kerner_settings.minimum_distance = atof (argv[++no]);
        }
      else if (!strcmp (argv[no], "-D"))
        {
          EXPECT_ARG;
          kerner_settings.maximum_distance = atof (argv[++no]);
        }
      else if (!strcmp (argv[no], "-o"))
        {
          EXPECT_ARG;
          kerner_settings.offset = atof (argv[++no]);
        }
      else if (!strcmp (argv[no], "-t"))
        {
          EXPECT_ARG;
          kerner_settings.tracking = atof (argv[++no]);
        }
      else if (!strcmp (argv[no], "-p"))
        {
          EXPECT_ARG;
          kerner_settings.alpha_target = atof (argv[++no]);
        }
      else if (!strcmp (argv[no], "-m"))
        {
          int i;
          char *method;
          int found = 0;
          EXPECT_ARG;
          method = argv[++no];
          for (i = 0; methods[i]; i++)
            {
              if (!strcmp (method, methods[i]->name))
                {
                  kerner_settings.method = methods[i];
                  found = 1;
                  break;
                }
            }
          if (!found)
            {
              fprintf (stderr, "unknown method %s\n", method);
              fprintf (stderr, "Available methods:");
              for (i = 0; methods[i]; i++)
                {
                  fprintf (stderr, " %s", methods[i]->name);
                }
              fprintf (stderr, "\n");
              exit (-1);
            }
        }
      else if (!strcmp (argv[no], "-l"))
        {
          kernagic_strip_left_bearing = 0;
        }
      else if (!strcmp (argv[no], "-L"))
        {
          kernagic_strip_left_bearing = 1;
        }
      else if (!strcmp (argv[no], "-T"))
      {
        EXPECT_ARG;
        kernagic_sample_text = argv[++no];
      }
      else if (!strcmp (argv[no], "-O"))
      {
        EXPECT_ARG;
        kernagic_output = argv[++no];
        interactive = 0;

        if (!ufo_path)
          {
            fprintf (stderr, "must specify input font before -o\n");
            exit (-1);
          }

        char cmd[512];
        /* XXX: does not work on windows */
        sprintf (cmd, "rm -rf %s;cp -Rva %s %s", kernagic_output, ufo_path, kernagic_output);
        fprintf (stderr, "%s\n", cmd);

        system (cmd);
        ufo_path = kernagic_output;
      }
      else if (!strcmp (argv[no], "-P"))
      {
        EXPECT_ARG;
        kernagic_output_png = argv[++no];
        interactive = 0;
      }
      else if (!strcmp (argv[no], "-s"))
      {
        EXPECT_ARG;
        kernagic_sample_string = argv[++no];
      }
      else if (argv[no][0] == '-')
        {
          fprintf (stderr, "unknown argument %s\n", argv[no]);
          exit (-1);
        }
      else
        {
          ufo_path = argv[no];
        }
    }
}

int ui_gtk (int argc, char **argv);
KernagicMethod *kernagic_method_no (int no)
{
  if (no < 0) no = 0;
  return methods[no];
}

int kernagic_find_method_no (KernagicMethod *method)
{
  int i;
  for (i = 0; methods[i]; i++)
    if (methods[i] == method)
      return i;
  return 0;
}

int kernagic_active_method_no (void)
{
  return kernagic_find_method_no (kerner_settings.method);
}

extern uint8_t   *kernagic_preview;

int main (int argc, char **argv)
{
  if (!strcmp (basename(argv[0]), "ipsumat"))
    return ipsumat (argc, argv);

  if (!kernagic_preview)
    kernagic_preview = g_malloc0 (PREVIEW_WIDTH * PREVIEW_HEIGHT);

  init_methods ();
  parse_args (argc, argv);

  if (interactive)
    return ui_gtk (argc, argv);

  if (!ufo_path)
    {
      fprintf (stderr, "no font file to work on specified\n");
      exit (-1);
    }

  kernagic_load_ufo (ufo_path, kernagic_strip_left_bearing);

  kernagic_set_glyph_string (NULL);
  kernagic_compute (NULL);

  if (kernagic_output_png)
    {
      int i;
      cairo_surface_t *surface =
        cairo_image_surface_create_for_data (kernagic_preview,
            CAIRO_FORMAT_A8, PREVIEW_WIDTH, PREVIEW_HEIGHT, PREVIEW_WIDTH);
      redraw_test_text ("", kernagic_sample_text, 0, 0); 
      for (i = 0; i < PREVIEW_WIDTH * PREVIEW_HEIGHT; i++)
        kernagic_preview[i] = 255 - kernagic_preview[i];
      cairo_surface_write_to_png (surface, kernagic_output_png);
      return 0;
    }
  kernagic_save_kerning_info ();
  return 0;
}

void kernagic_compute (GtkProgressBar *progress)
{
  GList *glyphs = kernagic_glyphs ();
  long int total = g_list_length (glyphs);
  long int count = 0;
  GList *left;

  if (kerner_settings.method->init)
    kerner_settings.method->init ();

  for (left = glyphs; left; left = left->next)
  {
    Glyph *lg = left->data;
    if (progress)
    {
      float fraction = count / (float)total;
      gtk_progress_bar_set_fraction (progress, fraction);
    }

    kernagic_glyph_reset (lg);
  
    if (progress || kernagic_deal_with_glyph (lg->unicode))
    {
      if (kerner_settings.method->each)
        kerner_settings.method->each (lg, progress);
    }
    count ++;
  }

  if (kerner_settings.method->done)
    kerner_settings.method->done ();

  /* space space to be with of i, if both glyphs exist */
  {
    Glyph *space = kernagic_find_glyph_unicode (' ');
    Glyph *i = kernagic_find_glyph_unicode ('i');

    if (i && space)
      {
        float desired_width = i->left_bearing + i->ink_width + i->right_bearing;

        float width;
        
        width = desired_width - i->ink_width;
        space->right_bearing = 0;
        space->left_bearing = width;
        fprintf (stderr, "!!!!\n");
      }
  }
}
