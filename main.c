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

    int drm_fd = open("/dev/dri/card1", O_RDWR);
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

    /* Check if we have any handles available to us. */
    int handle_index_check;
    for (handle_index_check = 0; handle_index_check < 4; handle_index_check++)
    {
        if (fb->handles[handle_index_check])
            break;
    }

    if (handle_index_check == 4) {
        log_error("Failed to find a valid handle\n");
        exit(1);
    }
            

    struct gbm_device* device = gbm_create_device(drm_fd);

    /* Output the first image that we find. */
    for (int handle_index = 0; handle_index < 4 && fb->handles[handle_index]; handle_index++) {
        int handle_fd;
        int err = drmPrimeHandleToFD(drm_fd, fb->handles[handle_index], 0, &handle_fd);
        if (err < 0) {
            log_error("Could not get prime handle from file descriptor\n");
            continue;
        }

        log_info("File Descriptor for the handle is: %d\n", handle_fd);
        
        // Establish a memory map.
        struct gbm_import_fd_data import_data = {
            handle_fd,
            fb->width,
            fb->height,
            fb->offsets[handle_index],
            fb->pixel_format};
        struct gbm_bo* bo = gbm_bo_import(
            device,
            GBM_BO_IMPORT_FD,
            &import_data,
            0);

        if (!bo)
        {
            log_error("gbm_bo_import failed: %d", errno);
            continue;
        }

        void* map_data;
        uint32_t map_stride;
        void* void_map = gbm_bo_map(
            bo,
            0,
            0,
            fb->width,
            fb->height,
            0,
            &map_stride,
            &map_data);

        if (void_map == MAP_FAILED)
        {
            gbm_bo_destroy(bo);
            log_error("gbm_bo_map failed: %d", errno);
            continue;
        }

        unsigned char* map = (unsigned char*)void_map;

        // Initialize the array of pixels
        unsigned char *img = NULL;
        int img_size = 3 * fb->width * fb->height;
        img = (unsigned char *)malloc(img_size);
        memset(img, 0, img_size);

        // Iterate over the framebuffer and add the pixels to the array.
        uint32_t size = map_stride * fb->height;
        uint32_t offset = 0;
        for (uint32_t y = 0; y < fb->height; y++) {
            uint32_t read_in_row = 0;
            for (uint32_t x = 0; x < fb->width; x++) {
                // Read 32 bits because DRM_FORMAT_XRGB8888
                unsigned char pixels[4];
                memcpy(&pixels[0], &map[offset], sizeof(unsigned char) * 4);
                uint32_t pixel_position = (fb->height - 1 - y) * (3 * fb->width) + (3 * x);
                img[pixel_position++] = pixels[0];
                img[pixel_position++] = pixels[1];
                img[pixel_position++] = pixels[2];
                read_in_row += sizeof(unsigned char) * 4;
                offset += sizeof(unsigned char) * 4;
            }
            
            if (read_in_row != fb->pitches[handle_index])
                log_error("Read the wrong pitch: %d\n", read_in_row);
        }

        if (offset != size)
            log_error("Failed to read the entire size!\n");

        generateBitmapImage(img, fb->height, fb->width, (char*)"output.bmp");
        free(img);
        gbm_bo_unmap(bo, map_data);
        gbm_bo_destroy(bo);
    }

    return 0;
}
