gst-launch-1.0 \
    -e \
    v4l2src device=/dev/video0 ! 'video/x-raw, format=YUY2, width=640, height=480, framerate=30/1' ! \
    nvvideoconvert ! 'video/x-raw(memory:NVMM), format=RGBA' ! \
    m.sink_0 nvstreammux name=m width=640 height=480 batch_size=1 \
    ! nvvideoconvert ! 'video/x-raw(memory:NVMM), format=RGBA' ! \
    dsrotate divide-count=6 ! \
    nvegltransform ! nveglglessink sync=0
