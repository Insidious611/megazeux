/* MegaZeux
 *
 * Copyright (C) 2006-2007 Gilead Kutnick <exophase@adelphia.net>
 * Copyright (C) 2007 Alistair John Strachan <alistair@devzero.co.uk>
 * Copyright (C) 2007 Alan Williams <mralert@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "render_gl.h"
#include "render.h"
#include "renderers.h"
#include "util.h"

#ifdef CONFIG_SDL
#include "render_sdl.h"
#endif

#ifdef CONFIG_EGL
#include "render_egl.h"
#endif

struct gl1_syms
{
  int syms_loaded;
  void (GLAPIENTRY *glBegin)(GLenum mode);
  void (GLAPIENTRY *glBindTexture)(GLenum target, GLuint texture);
  void (GLAPIENTRY *glClear)(GLbitfield mask);
  void (GLAPIENTRY *glEnable)(GLenum cap);
  void (GLAPIENTRY *glEnd)(void);
  void (GLAPIENTRY *glGenTextures)(GLsizei n, GLuint *textures);
  const GLubyte* (GLAPIENTRY *glGetString)(GLenum name);
  void (GLAPIENTRY *glTexCoord2f)(GLfloat s, GLfloat t);
  void (GLAPIENTRY *glTexImage2D)(GLenum target, GLint level,
   GLint internalformat,GLsizei width, GLsizei height, GLint border,
   GLenum format, GLenum type, const GLvoid *pixels);
  void (GLAPIENTRY *glTexParameteri)(GLenum target, GLenum pname, GLint param);
  void (GLAPIENTRY *glVertex3f)(GLfloat x, GLfloat y, GLfloat z);
  void (GLAPIENTRY *glViewport)(GLint x, GLint y, GLsizei width, GLsizei height);
};

struct gl1_render_data
{
  Uint32 *pixels;
  Uint32 w;
  Uint32 h;
  struct gl1_syms gl;
  enum ratio_type ratio;
};

static int gl1_load_syms (struct gl1_syms *gl)
{
  if(gl->syms_loaded)
    return true;

  // Since 1.0
  GL_LOAD_SYM(gl, glBegin)
  // Since 1.1
  GL_LOAD_SYM(gl, glBindTexture)
  // Since 1.0
  GL_LOAD_SYM(gl, glClear)
  // Since 1.0 (parameters may require more recent version)
  GL_LOAD_SYM(gl, glEnable)
  // Since 1.0
  GL_LOAD_SYM(gl, glEnd)
  // Since 1.1
  GL_LOAD_SYM(gl, glGenTextures)
  // Since 1.0
  GL_LOAD_SYM(gl, glGetString)
  // Since 1.0
  GL_LOAD_SYM(gl, glTexCoord2f)
  // Since 1.0 (parameters may require more recent version)
  GL_LOAD_SYM(gl, glTexImage2D)
  // Since 1.0
  GL_LOAD_SYM(gl, glTexParameteri)
  // Since 1.0
  GL_LOAD_SYM(gl, glVertex3f)
  // Since 1.0
  GL_LOAD_SYM(gl, glViewport)

  gl->syms_loaded = true;
  return true;
}

static bool gl1_init_video(struct graphics_data *graphics,
 struct config_info *conf)
{
  struct gl1_render_data *render_data = malloc(sizeof(struct gl1_render_data));
  struct gl1_syms *gl = &render_data->gl;
  int internal_width, internal_height;
  const char *version, *extensions;

  if(!render_data)
    goto err_out;

  if(!GL_CAN_USE)
    goto err_free_render_data;

  graphics->render_data = render_data;
  render_data->ratio = conf->video_ratio;
  gl->syms_loaded = false;

  graphics->gl_vsync = conf->gl_vsync;
  graphics->allow_resize = conf->allow_resize;
  graphics->gl_filter_method = conf->gl_filter_method;
  graphics->bits_per_pixel = 32;

  // OpenGL only supports 16/32bit colour
  if(conf->force_bpp == 16 || conf->force_bpp == 32)
    graphics->bits_per_pixel = conf->force_bpp;

  if(!set_video_mode())
    goto err_free_render_data;

  // NOTE: These must come AFTER set_video_mode()!
  version = (const char *)gl->glGetString(GL_VERSION);
  extensions = (const char *)gl->glGetString(GL_EXTENSIONS);

  // we need a specific "version" of OpenGL compatibility
  if(version && atof(version) < 1.1)
  {
    warn("Your OpenGL implementation is too old (need v1.1).\n");
    goto err_free_render_data;
  }

  // we also might be able to utilise an extension
  if(extensions && strstr(extensions, "GL_ARB_texture_non_power_of_two"))
  {
    internal_width = GL_NON_POWER_2_WIDTH;
    internal_height = GL_NON_POWER_2_HEIGHT;
  }
  else
  {
    internal_width = GL_POWER_2_WIDTH;
    internal_height = GL_POWER_2_HEIGHT;
  }

  render_data->pixels = malloc(sizeof(Uint32) * internal_width *
   internal_height);
  render_data->w = internal_width;
  render_data->h = internal_height;

  if(!render_data->pixels)
    goto err_free_render_data;

  return true;

err_free_render_data:
  free(render_data);
err_out:
  return false;
}

static bool gl1_set_video_mode(struct graphics_data *graphics,
 int width, int height, int depth, bool fullscreen, bool resize)
{
  struct gl1_render_data *render_data = graphics->render_data;
  struct gl1_syms *gl = &render_data->gl;
  int v_width, v_height;

  GLuint texture_number;

  gl_set_attributes(graphics);

  if(!gl_set_video_mode(graphics, width, height, depth, fullscreen, resize))
    return false;

  if(!gl1_load_syms(gl))
    return false;

  fix_viewport_ratio(width, height, &v_width, &v_height, render_data->ratio);

  gl->glViewport((width - v_width) >> 1, (height - v_height) >> 1,
   v_width, v_height);

  gl->glEnable(GL_TEXTURE_2D);

  gl->glGenTextures(1, &texture_number);
  gl->glBindTexture(GL_TEXTURE_2D, texture_number);

  gl_set_filter_method(graphics->gl_filter_method, gl->glTexParameteri);

  return true;
}

static void gl1_free_video(struct graphics_data *graphics)
{
  free(graphics->render_data);
  graphics->render_data = NULL;
}

static void gl1_update_colors(struct graphics_data *graphics,
 struct rgb_color *palette, Uint32 count)
{
  Uint32 i;
  for(i = 0; i < count; i++)
  {
#if PLATFORM_BYTE_ORDER == PLATFORM_BIG_ENDIAN
    graphics->flat_intensity_palette[i] = (palette[i].r << 24) |
     (palette[i].g << 16) | (palette[i].b << 8);
#else
    graphics->flat_intensity_palette[i] = (palette[i].b << 16) |
     (palette[i].g << 8) | palette[i].r;
#endif
  }
}

static void gl1_render_graph(struct graphics_data *graphics)
{
  struct gl1_render_data *render_data = graphics->render_data;
  Uint32 mode = graphics->screen_mode;

  if(!mode)
  {
    render_graph32(render_data->pixels, render_data->w * 4, graphics,
     set_colors32[mode]);
  }
  else
  {
    render_graph32s(render_data->pixels, render_data->w * 4, graphics,
     set_colors32[mode]);
  }
}

static void gl1_render_cursor(struct graphics_data *graphics,
 Uint32 x, Uint32 y, Uint8 color, Uint8 lines, Uint8 offset)
{
  struct gl1_render_data *render_data = graphics->render_data;

  render_cursor(render_data->pixels, render_data->w * 4, 32, x, y,
   graphics->flat_intensity_palette[color], lines, offset);
}

static void gl1_render_mouse(struct graphics_data *graphics,
 Uint32 x, Uint32 y, Uint8 w, Uint8 h)
{
  struct gl1_render_data *render_data = graphics->render_data;

  render_mouse(render_data->pixels, render_data->w * 4, 32, x, y, 0xFFFFFFFF,
   w, h);
}

static void gl1_sync_screen(struct graphics_data *graphics)
{
  struct gl1_render_data *render_data = graphics->render_data;
  struct gl1_syms *gl = &render_data->gl;
  float texture_width, texture_height;

  texture_width = (float)640 / render_data->w;
  texture_height = (float)350 / render_data->h;

  gl->glTexImage2D(GL_TEXTURE_2D, 0, 3, render_data->w, render_data->h, 0,
   GL_RGBA, GL_UNSIGNED_BYTE, render_data->pixels);

  gl->glClear(GL_COLOR_BUFFER_BIT);

  gl->glBegin(GL_TRIANGLE_STRIP);
    gl->glTexCoord2f(0.0, 0.0);
    gl->glVertex3f(-1.0, 1.0, 0.0);

    gl->glTexCoord2f(0.0, texture_height);
    gl->glVertex3f(-1.0, -1.0, 0.0);

    gl->glTexCoord2f(texture_width, 0.0);
    gl->glVertex3f(1.0, 1.0, 0.0);

    gl->glTexCoord2f(texture_width, texture_height);
    gl->glVertex3f(1.0, -1.0, 0.0);
  gl->glEnd();

  gl_swap_buffers(graphics);
}

void render_gl1_register(struct renderer *renderer)
{
  memset(renderer, 0, sizeof(struct renderer));
  renderer->init_video = gl1_init_video;
  renderer->free_video = gl1_free_video;
  renderer->check_video_mode = gl_check_video_mode;
  renderer->set_video_mode = gl1_set_video_mode;
  renderer->update_colors = gl1_update_colors;
  renderer->resize_screen = resize_screen_standard;
  renderer->get_screen_coords = get_screen_coords_scaled;
  renderer->set_screen_coords = set_screen_coords_scaled;
  renderer->render_graph = gl1_render_graph;
  renderer->render_cursor = gl1_render_cursor;
  renderer->render_mouse = gl1_render_mouse;
  renderer->sync_screen = gl1_sync_screen;
}
