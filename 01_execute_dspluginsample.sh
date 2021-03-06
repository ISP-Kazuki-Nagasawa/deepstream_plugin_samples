gst-launch-1.0 \
    -e \
    filesrc location = /opt/nvidia/deepstream/deepstream/samples/streams/sample_1080p_h264.mp4 ! \
    qtdemux ! h264parse ! nvv4l2decoder ! m.sink_0 nvstreammux name=m width=1280 height=720 batch_size=1 ! \
    nvinfer config-file-path=config_infer_primary.txt ! nvvideoconvert ! 'video/x-raw(memory:NVMM), format=RGBA' ! \
    dspluginsample ! \
    nvdsosd ! nvegltransform ! nveglglessink sync=0
