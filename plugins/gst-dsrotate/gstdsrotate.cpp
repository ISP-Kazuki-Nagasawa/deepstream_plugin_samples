/**
 * Copyright (c) 2017-2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include <string>
#include <sstream>
#include <iostream>
#include <ostream>
#include <fstream>
#include "gstdsrotate.h"
#include <sys/time.h>

GST_DEBUG_CATEGORY_STATIC (gst_dsrotate_debug);
#define GST_CAT_DEFAULT gst_dsrotate_debug
#define USE_EGLIMAGE 1
static GQuark _dsmeta_quark = 0;

/* Enum to identify properties */
enum
{
  PROP_0,
  PROP_UNIQUE_ID,
  PROP_GPU_DEVICE_ID,
  PROP_DIVIDE_COUNT
};

#define CHECK_NVDS_MEMORY_AND_GPUID(object, surface)  \
  ({ int _errtype=0;\
   do {  \
    if ((surface->memType == NVBUF_MEM_DEFAULT || surface->memType == NVBUF_MEM_CUDA_DEVICE) && \
        (surface->gpuId != object->gpu_id))  { \
    GST_ELEMENT_ERROR (object, RESOURCE, FAILED, \
        ("Input surface gpu-id doesnt match with configured gpu-id for element," \
         " please allocate input using unified memory, or use same gpu-ids"),\
        ("surface-gpu-id=%d,%s-gpu-id=%d",surface->gpuId,GST_ELEMENT_NAME(object),\
         object->gpu_id)); \
    _errtype = 1;\
    } \
    } while(0); \
    _errtype; \
  })

/* Default values for properties */
#define DEFAULT_UNIQUE_ID 15
#define DEFAULT_GPU_ID 0
#define DEFAULT_DIVIDE_COUNT 1

#define CHECK_NPP_STATUS(npp_status,error_str) do { \
  if ((npp_status) != NPP_SUCCESS) { \
    g_print ("Error: %s in %s at line %d: NPP Error %d\n", \
        error_str, __FILE__, __LINE__, npp_status); \
    goto error; \
  } \
} while (0)

#define CHECK_CUDA_STATUS(cuda_status,error_str) do { \
  if ((cuda_status) != cudaSuccess) { \
    g_print ("Error: %s in %s at line %d (%s)\n", \
        error_str, __FILE__, __LINE__, cudaGetErrorName(cuda_status)); \
    goto error; \
  } \
} while (0)

/* By default NVIDIA Hardware allocated memory flows through the pipeline. We
 * will be processing on this type of memory only. */
#define GST_CAPS_FEATURE_MEMORY_NVMM "memory:NVMM"
static GstStaticPadTemplate gst_dsrotate_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM,
            "{ NV12, RGBA, I420 }")));

static GstStaticPadTemplate gst_dsrotate_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_NVMM,
            "{ NV12, RGBA, I420 }")));

/* Define our element type. Standard GObject/GStreamer boilerplate stuff */
#define gst_dsrotate_parent_class parent_class
G_DEFINE_TYPE (GstDsRotate, gst_dsrotate, GST_TYPE_BASE_TRANSFORM);

static void gst_dsrotate_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dsrotate_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_dsrotate_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_dsrotate_start (GstBaseTransform * btrans);
static gboolean gst_dsrotate_stop (GstBaseTransform * btrans);

static GstFlowReturn gst_dsrotate_transform_ip (GstBaseTransform *
    btrans, GstBuffer * inbuf);

/* Install properties, set sink and src pad capabilities, override the required
 * functions of the base class, These are common to all instances of the
 * element.
 */
static void
gst_dsrotate_class_init (GstDsRotateClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  /* Indicates we want to use DS buf api */
  g_setenv ("DS_NEW_BUFAPI", "1", TRUE);

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  /* Overide base class functions */
  gobject_class->set_property = GST_DEBUG_FUNCPTR (gst_dsrotate_set_property);
  gobject_class->get_property = GST_DEBUG_FUNCPTR (gst_dsrotate_get_property);

  gstbasetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_dsrotate_set_caps);
  gstbasetransform_class->start = GST_DEBUG_FUNCPTR (gst_dsrotate_start);
  gstbasetransform_class->stop = GST_DEBUG_FUNCPTR (gst_dsrotate_stop);

  gstbasetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_dsrotate_transform_ip);

  /* Install properties */
  g_object_class_install_property (gobject_class, PROP_UNIQUE_ID,
      g_param_spec_uint ("unique-id",
          "Unique ID",
          "Unique ID for the element. Can be used to identify output of the"
          " element", 0, G_MAXUINT, DEFAULT_UNIQUE_ID, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_GPU_DEVICE_ID,
      g_param_spec_uint ("gpu-id",
          "Set GPU Device ID",
          "Set GPU Device ID", 0,
          G_MAXUINT, 0,
          GParamFlags
          (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_DIVIDE_COUNT,
      g_param_spec_uint ("divide-count",
          "Set rotation divide count",
          "Set rotation divide count", 
          1, G_MAXUINT, DEFAULT_DIVIDE_COUNT, 
          GParamFlags
          (G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  /* Set sink and src pad capabilities */
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_dsrotate_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_dsrotate_sink_template));

  /* Set metadata describing the element */
  gst_element_class_set_details_simple (gstelement_class,
      "DsRotate plugin",
      "DsRotate Plugin",
      "Process a 3rdparty rotate algorithm",
      "NVIDIA Corporation. Post on Deepstream for Tesla forum for any queries "
      "@ https://devtalk.nvidia.com/default/board/209/");
}

static void
gst_dsrotate_init (GstDsRotate * dsrotate)
{
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (dsrotate);

  /* We will not be generating a new buffer. Just adding / updating
   * metadata. */
  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (btrans), TRUE);
  /* We do not want to change the input caps. Set to passthrough. transform_ip
   * is still called. */
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (btrans), TRUE);

  /* Initialize all property variables to default values */
  dsrotate->unique_id = DEFAULT_UNIQUE_ID;
  dsrotate->gpu_id = DEFAULT_GPU_ID;
  dsrotate->divide_count = DEFAULT_DIVIDE_COUNT;
  /* This quark is required to identify NvDsMeta when iterating through
   * the buffer metadatas */
  if (!_dsmeta_quark)
    _dsmeta_quark = g_quark_from_static_string (NVDS_META_STRING);
}

/* Function called when a property of the element is set. Standard boilerplate.
 */
static void
gst_dsrotate_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDsRotate *dsrotate = GST_DSROTATE (object);
  switch (prop_id) {
    case PROP_UNIQUE_ID:
      dsrotate->unique_id = g_value_get_uint (value);
      break;
    case PROP_GPU_DEVICE_ID:
      dsrotate->gpu_id = g_value_get_uint (value);
      break;
    case PROP_DIVIDE_COUNT:
      dsrotate->divide_count = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* Function called when a property of the element is requested. Standard
 * boilerplate.
 */
static void
gst_dsrotate_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDsRotate *dsrotate = GST_DSROTATE (object);

  switch (prop_id) {
    case PROP_UNIQUE_ID:
      g_value_set_uint (value, dsrotate->unique_id);
      break;
    case PROP_GPU_DEVICE_ID:
      g_value_set_uint (value, dsrotate->gpu_id);
      break;
    case PROP_DIVIDE_COUNT:
      g_value_set_uint (value, dsrotate->divide_count);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * Initialize all resources and start the output thread
 */
static gboolean
gst_dsrotate_start (GstBaseTransform * btrans)
{
  GstDsRotate *dsrotate = GST_DSROTATE (btrans);

  GstQuery *queryparams = NULL;
  guint batch_size = 1;

  CHECK_CUDA_STATUS (cudaSetDevice (dsrotate->gpu_id),
      "Unable to set cuda device");

  dsrotate->batch_size = 1;
  queryparams = gst_nvquery_batch_size_new ();
  if (gst_pad_peer_query (GST_BASE_TRANSFORM_SINK_PAD (btrans), queryparams)
      || gst_pad_peer_query (GST_BASE_TRANSFORM_SRC_PAD (btrans), queryparams)) {
    if (gst_nvquery_batch_size_parse (queryparams, &batch_size)) {
      dsrotate->batch_size = batch_size;
    }
  }
  GST_DEBUG_OBJECT (dsrotate, "Setting batch-size %d \n",
      dsrotate->batch_size);
  gst_query_unref (queryparams);

  return TRUE;

error:
  return FALSE;
}

/**
 * Stop the output thread and free up all the resources
 */
static gboolean
gst_dsrotate_stop (GstBaseTransform * btrans)
{
  GstDsRotate *dsrotate = GST_DSROTATE (btrans);
  if (dsrotate->rotator != nullptr) {
    delete dsrotate->rotator;
    dsrotate->rotator = nullptr;
  }

  return TRUE;
}

/**
 * Called when source / sink pad capabilities have been negotiated.
 */
static gboolean
gst_dsrotate_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstDsRotate *dsrotate = GST_DSROTATE (btrans);
  /* Save the input video information, since this will be required later. */
  gst_video_info_from_caps (&dsrotate->video_info, incaps);

  return TRUE;
}

/**
 * Called when element recieves an input buffer from upstream element.
 */
static GstFlowReturn
gst_dsrotate_transform_ip (GstBaseTransform * btrans, GstBuffer * inbuf)
{
  GstDsRotate *dsrotate = GST_DSROTATE (btrans);
  GstMapInfo in_map_info;
  GstFlowReturn flow_ret = GST_FLOW_ERROR;

  NvBufSurface *surface = NULL;
  NvDsBatchMeta *batch_meta = NULL;
  NvDsFrameMeta *frame_meta = NULL;
  NvDsMetaList * l_frame = NULL;

  dsrotate->frame_num++;
  CHECK_CUDA_STATUS (cudaSetDevice (dsrotate->gpu_id),
      "Unable to set cuda device");

  memset (&in_map_info, 0, sizeof (in_map_info));
  if (!gst_buffer_map (inbuf, &in_map_info, GST_MAP_READ)) {
    g_print ("Error: Failed to map gst buffer\n");
    goto error;
  }

  nvds_set_input_system_timestamp (inbuf, GST_ELEMENT_NAME (dsrotate));
  surface = (NvBufSurface *) in_map_info.data;
  GST_DEBUG_OBJECT (dsrotate,
      "Processing Frame %" G_GUINT64_FORMAT " Surface %p\n",
      dsrotate->frame_num, surface);

  if (CHECK_NVDS_MEMORY_AND_GPUID (dsrotate, surface))
    goto error;

  batch_meta = gst_buffer_get_nvds_batch_meta (inbuf);
  if (batch_meta == nullptr) {
    GST_ELEMENT_ERROR (dsrotate, STREAM, FAILED,
        ("NvDsBatchMeta not found for input buffer."), (NULL));
    return GST_FLOW_ERROR;
  }

  {
    NvDsMetaList * l_obj = NULL;
    NvDsObjectMeta *obj_meta = NULL;

#ifndef __aarch64__
    if (surface->memType != NVBUF_MEM_CUDA_UNIFIED){
      GST_ELEMENT_ERROR (dsrotate, STREAM, FAILED,
        ("%s:need NVBUF_MEM_CUDA_UNIFIED memory for opencv blurring",__func__), (NULL));
      return GST_FLOW_ERROR;
    }
#endif

    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL; l_frame = l_frame->next) {
      frame_meta = (NvDsFrameMeta *) (l_frame->data);
      cv::Mat in_mat;

      if (surface->surfaceList[frame_meta->batch_id].mappedAddr.addr[0] == NULL){
        if (NvBufSurfaceMap (surface, frame_meta->batch_id, 0, NVBUF_MAP_READ_WRITE) != 0){
            GST_ELEMENT_ERROR (dsrotate, STREAM, FAILED,
              ("%s:buffer map to be accessed by CPU failed", __func__), (NULL));
            return GST_FLOW_ERROR;
        }
      }

      /* Cache the mapped data for CPU access */
      NvBufSurfaceSyncForCpu (surface, frame_meta->batch_id, 0);

      /*** OpenCV access ***/
      in_mat =
          cv::Mat (surface->surfaceList[frame_meta->batch_id].planeParams.height[0],
             surface->surfaceList[frame_meta->batch_id].planeParams.width[0], CV_8UC4,
             surface->surfaceList[frame_meta->batch_id].mappedAddr.addr[0],
             surface->surfaceList[frame_meta->batch_id].planeParams.pitch[0]);

      float width = surface->surfaceList[frame_meta->batch_id].planeParams.width[0];
      float height = surface->surfaceList[frame_meta->batch_id].planeParams.height[0];

      // Video rotator
      if (dsrotate->rotator == nullptr) {
        dsrotate->rotator = new VideoRotator(width, height, dsrotate->divide_count);
      }
      dsrotate->rotator->rotate(in_mat);

      /* Cache the mapped data for device access */
      NvBufSurfaceSyncForDevice(surface, frame_meta->batch_id, 0);
    }
  }
  flow_ret = GST_FLOW_OK;

error:
  nvds_set_output_system_timestamp (inbuf, GST_ELEMENT_NAME (dsrotate));
  gst_buffer_unmap (inbuf, &in_map_info);
  return flow_ret;
}

/**
 * Boiler plate for registering a plugin and an element.
 */
static gboolean
dsrotate_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_dsrotate_debug, "dsrotate", 0,
      "dsrotate plugin");

  return gst_element_register (plugin, "dsrotate", GST_RANK_PRIMARY,
      GST_TYPE_DSROTATE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    nvdsgst_dsrotate,
    DESCRIPTION, dsrotate_plugin_init, DS_VERSION, LICENSE, BINARY_PACKAGE, URL)
