#include <fcntl.h>
#include <gbm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>
#include "bitmap.h"
#include <string.h>
#include <gbm.h>
#include <errno.h>

#define log_info(...) printf(__VA_ARGS__)
#define log_error(...) fprintf(stderr, __VA_ARGS__)

int main(int argc, char **argv)
{
    log_info("Reading the contents of the screen...\n");
    
    drmModeRes *resources = NULL;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    drmModeCrtc *crtc = NULL;
    struct _drmModeFB2 *fb;
    uint32_t buffer_id = 0;

    int drm_fd = open("/dev/dri/card0", O_RDWR);
    if (drm_fd < 0) {
        log_error("Unable to open DRI device\n");
        exit(1);
    }

    resources = drmModeGetResources(drm_fd);

    /* Find the first active connector to display on. */

    int connector_index = 0;
    for (; connector_index < resources->count_connectors; connector_index++) {
        connector = drmModeGetConnector(drm_fd, resources->connectors[connector_index]);
        if (connector == NULL)
            continue;

        if (connector->connection == DRM_MODE_CONNECTED &&
            connector->count_modes > 0)
            break;

        drmModeFreeConnector(connector);
    }

    if (connector_index == resources->count_connectors) {
        log_error("Could not find an active connector\n");
        exit(1);
    }

    /* Find the CRTC. */
    encoder = drmModeGetEncoder(drm_fd, connector->encoder_id);
    crtc = drmModeGetCrtc(drm_fd, encoder->crtc_id);
    buffer_id = crtc->buffer_id;

    log_info("Buffer id for CRTC is: %d\n", buffer_id);
    
    fb = drmModeGetFB2(drm_fd, buffer_id);

    log_info("Got the framebuffer, id=%d, width=%d, height=%d\n", fb->fb_id, fb->width, fb->height);
    log_info("DRM handles: %i, %i, %i, %i\n", fb->handles[0], fb->handles[1], fb->handles[2], fb->handles[3]);

    /* Check if we have any handles available to us. */
    int num_handles = 0;
    while (fb->handles[num_handles])
    {
        num_handles++;
    }
    if (num_handles == 0) {
        log_error("Failed to find a valid handle\n");
        exit(1);
    }

    /* Map GEM handles to prime FDs */
    struct handle_to_fd
    {
        int handle;
        int fd;
    };
    struct handle_to_fd handle_to_fd[4] = {0};
 
    for (int i = 0; i < 4; ++i)
    {
        int handle = fb->handles[i];
        if (handle)
        {
            struct handle_to_fd* entry = handle_to_fd;
            while(entry->handle && (entry->handle != handle))
            {
                ++entry;
            }
            if (!entry->handle)
            {
                entry->handle = handle;
                int err = drmPrimeHandleToFD(drm_fd, entry->handle, 0, &entry->fd);
                if (err)
                {
                    log_error("Could not get prime fd from handle: %s (%i)\n", strerror(err), err);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }

            

    struct gbm_device* device = gbm_create_device(drm_fd);

    /* Output the first image that we find. */
    int plane_fds[4] = {0};
    for (int i = 0; i < num_handles; ++i)
    {
        struct handle_to_fd* entry = handle_to_fd;
        while (entry->handle != fb->handles[i])
        {
            ++entry;
        }
        plane_fds[i] = entry->fd;
    }
    
    struct gbm_import_fd_modifier_data import_data = {
       .width=fb->width,
       .height=fb->height,
       .format=fb->pixel_format,
       .num_fds=num_handles,
       .fds={plane_fds[0], plane_fds[1], plane_fds[2], plane_fds[3]},
       .strides={fb->pitches[0], fb->pitches[1], fb->pitches[2], fb->pitches[3]},
       .offsets={fb->offsets[0], fb->offsets[1], fb->offsets[2], fb->offsets[3]},
       .modifier=fb->modifier
    };
            
    struct gbm_bo* bo = gbm_bo_import(
        device,
        GBM_BO_IMPORT_FD_MODIFIER,
        &import_data,
        0);

    if (!bo)
    {
        log_error("gbm_bo_import failed: %d", errno);
        exit(EXIT_FAILURE);
    }

    void* map_data;
    uint32_t map_stride;
    void* void_map = gbm_bo_map(
        bo,
        0,
        0,
        fb->width,
        fb->height,
        GBM_BO_TRANSFER_READ,
        &map_stride,
        &map_data);

    if (void_map == MAP_FAILED)
    {
        gbm_bo_destroy(bo);
        log_error("gbm_bo_map failed: %d", errno);
        exit(EXIT_FAILURE);
    }

    unsigned char* map = (unsigned char*)void_map;

    // Initialize the array of pixels
    unsigned char *img = NULL;
    int img_size = 3 * fb->width * fb->height;
    img = (unsigned char *)malloc(img_size);
    memset(img, 0, img_size);

    // Iterate over the framebuffer and add the pixels to the array.
    for (uint32_t y = 0; y < fb->height; y++) {
        unsigned char* row = map + (map_stride * y);
        for (uint32_t x = 0; x < fb->width; x++) {
            unsigned char* pixel = row + (4 * x);
            // Read 32 bits because DRM_FORMAT_XRGB8888
            uint32_t pixel_position = (fb->height - 1 - y) * (3 * fb->width) + (3 * x);
            img[pixel_position++] = pixel[0];
            img[pixel_position++] = pixel[1];
            img[pixel_position++] = pixel[2];
        }            
    }

    generateBitmapImage(img, fb->height, fb->width, (char*)"output.bmp");
    free(img);
    gbm_bo_unmap(bo, map_data);
    gbm_bo_destroy(bo);

    return 0;
}
