/* gcc -o drawinbuffer drawinbuffer.cA $(pkg-config --cflags --libs glib-2.0
 * libdrm) */

#include <fcntl.h>
#include <gbm.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm/drm_fourcc.h>
#include <jpeglib.h>
#include "bitmap.h"

#define log_info(...) printf(__VA_ARGS__)
#define log_error(...) fprintf(stderr, __VA_ARGS__)

int main(int argc, char **argv)
{
    log_info("Reading the contents of the screen...\n");
    
    int drm_fd;
    drmModeRes *resources = NULL;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    drmModeCrtc *crtc = NULL;
    struct _drmModeFB2 *fb;
    uint32_t buffer_id = 0;

    drm_fd = open("/dev/dri/card0", O_RDWR);
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
            

    int err;
    for (int handle_index = 0; handle_index < 4 && fb->handles[handle_index]; handle_index++) {
        bool dup = false;

        for (int other_handle_index = 0; other_handle_index < handle_index; other_handle_index++) {
            if (fb->handles[handle_index] == fb->handles[other_handle_index]) {
                dup = true;
                break;
            }
        }

        if (dup) {
            // Duplicate...
            continue;
        }

        int handle_fd;
        err = drmPrimeHandleToFD(drm_fd, fb->handles[handle_index], 0, &handle_fd);
        if (err < 0) {
            log_error("Could not get prime handle from file descriptor\n");
            continue;
        }

        log_info("File Descriptor for the handle is: %d\n", handle_fd);
        struct gbm_device *device = gbm_create_device(drm_fd);

        if (!device) {
            log_error("Could not find a GBM device for the file descriptor");
            continue;
        }

        size_t size = fb->height * fb->pitches[handle_index];
        char *map = mmap(0, size, PROT_READ, MAP_SHARED, handle_fd, fb->offsets[handle_index]);

        log_info("Mapped memory successfully\n");
        log_info("pixel_format=%d, offset=%u, pitch=%u\n", fb->pixel_format, fb->offsets[handle_index], fb->pitches[handle_index]);

        if (fb->pixel_format != DRM_FORMAT_XRGB8888) {
            log_error("Unsupported pixel format\n");
            exit(1);
        }

        // Image array
        unsigned char *img = NULL;
        img = (unsigned char *)malloc(3 * fb->width * fb->height);
        memset(img, 0, 3 * fb->width * fb->height);
        uint32_t img_index = 0;

        uint32_t offset = 0;
        for (uint32_t y = 0; y < fb->height; y++) {
            for (uint32_t x = 0; x < fb->width; x++) {
                // Read 32 bits because DRM_FORMAT_XRGB8888
                uint8_t pixels[3];
                memcpy(&pixels[0], &map[offset], sizeof(uint8_t) * 3);
                img[img_index++] = pixels[0];
                img[img_index++] = pixels[1];
                img[img_index++] = pixels[2];
                offset += 4;
                continue;
                
                uint8_t b = map[offset];
                offset += 1;
                uint8_t g = map[offset];
                offset += 1;
                uint8_t r = map[offset];
                offset += 1;

                /* b = ((float)x) / ((float) fb->width) * 255.f; */
                /* g = ((float)y) / ((float) fb->height) * 255.f; */
                /* r = 255.f - ((float)x) / ((float) fb->width) * 255.f; */
                
                img[img_index] = b;
                img_index++;
                img[img_index] = g;
                img_index++;
                img[img_index] = r;
                img_index++;
                printf("%d, %d, %d\n", r, g, b);
            }
        }

        generateBitmapImage(img, fb->height, fb->width, (char*)"output.bmp");
        free(img);
        // TODO: Unmap

        bool new_way = true;
        if (new_way) continue;

        struct gbm_import_fd_data data = {handle_fd, fb->width, fb->height,
                                          fb->offsets[handle_index], fb->pixel_format};
        struct gbm_bo *bo = gbm_bo_import(device, GBM_BO_IMPORT_FD, &data, 0);

        if (!bo) {
            log_error("Could not import a buffer object for the file descriptor");
            continue;
        }

        uint32_t stride;
        void *map_data = NULL;
        void *addr = gbm_bo_map(bo, 0, 0, fb->width, fb->height, 0, &stride, &map_data);
        if (addr == MAP_FAILED) {
            log_error("Could not map the buffer object");
            continue;
        }

        if (map_data == NULL) {
            log_error("Mapped data was null");
            continue;
        }

        if (stride <= 0) {
            log_error("Stride is less than zero");
            continue;
        }

        uint32_t *pixel = (uint32_t *)addr;
        uint32_t pixel_size = sizeof(*pixel);
        log_info("Beginning map over region: height=%d, width=%d\n", fb->height, fb->width);
        log_info("stride=%d, pixel_size=%d", stride, pixel_size);
        for (uint32_t y = 0; y < fb->height; y++) {
            //log_info("[ ");
            for (uint32_t x = 0; x < fb->width; x++) {
                uint32_t index = y * (stride / pixel_size) + x;
                //log_info("%d, ", pixel[index]);
            }
            //log_info(" ]\n");
        }

        gbm_bo_unmap(bo, map_data);
    }

    return 0;
}
