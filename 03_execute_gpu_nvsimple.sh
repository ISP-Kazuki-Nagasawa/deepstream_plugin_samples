gst-launch-1.0 \
    -e \
    v4l2src device=/dev/video0 ! 'video/x-raw, width=640, height=480, format=YUY2, framerate=30/1' ! \
    nvvideoconvert ! 'video/x-raw, width=640, height=480, format=I420' ! \
    omxh264enc ! h264parse ! \
    omxh264dec ! nvivafilter cuda-process=true customer-lib-name="./gpu_plugins/nvsimple_cudaprocess/libnvsimple_cudaprocess.so" ! \
    'video/x-raw(memory:NVMM), format=(string)RGBA' ! \
    nvegltransform ! nveglglessink sync=0
