/*
Copyright (C) 2014 by Leonhard Oelke <leonhard@in-verted.de>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdlib.h>
#include <stdio.h>

#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>

#include <obs.h>
#include "xcursor.h"

#define XSHM_DATA(voidptr) struct xshm_data *data = voidptr;

struct xshm_data {
	Display *dpy;
	Window root_window;
	uint32_t width, height;
	int shm_attached;
	XShmSegmentInfo shm_info;
	XImage *image;
	texture_t texture;
	xcursor_t *cursor;
};

static const char* xshm_getname(const char* locale)
{
	UNUSED_PARAMETER(locale);
	return "X11 Shared Memory Screen Input";
}

static void xshm_destroy(void *vptr)
{
	XSHM_DATA(vptr);

	if (!data)
		return;

	gs_entercontext(obs_graphics());

	texture_destroy(data->texture);
	xcursor_destroy(data->cursor);

	gs_leavecontext();

	if (data->shm_attached)
		XShmDetach(data->dpy, &data->shm_info);

	if (data->shm_info.shmaddr != (char *) -1) {
		shmdt(data->shm_info.shmaddr);
		data->shm_info.shmaddr = (char *) -1;
	}

	if (data->shm_info.shmid != -1)
		shmctl(data->shm_info.shmid, IPC_RMID, NULL);

	if (data->image)
		XDestroyImage(data->image);

	if (data->dpy)
		XCloseDisplay(data->dpy);

	bfree(data);
}

static void *xshm_create(obs_data_t settings, obs_source_t source)
{
	UNUSED_PARAMETER(settings);
	UNUSED_PARAMETER(source);


	struct xshm_data *data = bmalloc(sizeof(struct xshm_data));
	memset(data, 0, sizeof(struct xshm_data));

	data->dpy = XOpenDisplay(NULL);
	if (!data->dpy)
		goto fail;

	Screen *screen = XDefaultScreenOfDisplay(data->dpy);
	data->width = WidthOfScreen(screen);
	data->height = HeightOfScreen(screen);
	data->root_window = XRootWindowOfScreen(screen);
	Visual *visual = DefaultVisualOfScreen(screen);
	int depth = DefaultDepthOfScreen(screen);

	if (!XShmQueryExtension(data->dpy))
		goto fail;

	data->image = XShmCreateImage(data->dpy, visual, depth,
		ZPixmap, NULL, &data->shm_info, data->width, data->height);
	if (!data->image)
		goto fail;

	data->shm_info.shmid = shmget(IPC_PRIVATE,
		data->image->bytes_per_line * data->image->height,
		IPC_CREAT | 0700);
	if (data->shm_info.shmid < 0)
		goto fail;

	data->shm_info.shmaddr
		= data->image->data
		= (char *) shmat(data->shm_info.shmid, 0, 0);
	if (data->shm_info.shmaddr == (char *) -1)
		goto fail;
	data->shm_info.readOnly = False;


	if (!XShmAttach(data->dpy, &data->shm_info))
		goto fail;
	data->shm_attached = 1;

	if (!XShmGetImage(data->dpy, data->root_window, data->image,
		0, 0, AllPlanes)) {
		goto fail;
	}


	gs_entercontext(obs_graphics());
	data->texture = gs_create_texture(data->width, data->height,
		GS_BGRA, 1, (const void**) &data->image->data, GS_DYNAMIC);
	data->cursor = xcursor_init(data->dpy);
	gs_leavecontext();

	if (!data->texture)
		goto fail;

	return data;

fail:
	xshm_destroy(data);
	return NULL;
}

static void xshm_video_tick(void *vptr, float seconds)
{
	UNUSED_PARAMETER(seconds);
	XSHM_DATA(vptr);

	gs_entercontext(obs_graphics());


	XShmGetImage(data->dpy, data->root_window, data->image,
		0, 0, AllPlanes);
	texture_setimage(data->texture, (void *) data->image->data,
		data->width * 4, False);

	xcursor_tick(data->cursor);

	gs_leavecontext();
}

static void xshm_video_render(void *vptr, effect_t effect)
{
	XSHM_DATA(vptr);

	eparam_t image = effect_getparambyname(effect, "image");
	effect_settexture(effect, image, data->texture);

	gs_enable_blending(False);

	gs_draw_sprite(data->texture, 0, 0, 0);

	xcursor_render(data->cursor);
}

static uint32_t xshm_getwidth(void *vptr)
{
	XSHM_DATA(vptr);

	return texture_getwidth(data->texture);
}

static uint32_t xshm_getheight(void *vptr)
{
	XSHM_DATA(vptr);

	return texture_getheight(data->texture);
}

struct obs_source_info xshm_input = {
    .id           = "xshm_input",
    .type         = OBS_SOURCE_TYPE_INPUT,
    .output_flags = OBS_SOURCE_VIDEO,
    .getname      = xshm_getname,
    .create       = xshm_create,
    .destroy      = xshm_destroy,
    .video_tick   = xshm_video_tick,
    .video_render = xshm_video_render,
    .getwidth     = xshm_getwidth,
    .getheight    = xshm_getheight
};
