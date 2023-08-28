# DRM Screen Shot

Take a screenshot of the first-found CRTC and save it as a `.bmp` file.

## Building
```
make
```

## Running
```
sudo ./drm_screen_shot
```

The screenshot will be output to `output.bmp`.

## Caveats
This doesn't appear to be work on a complex desktop, but it will work when you want to take a picture in a KMS-only situation (think `kmscube` or `plymouth`).
