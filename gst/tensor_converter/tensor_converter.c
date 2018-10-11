/**
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2018 MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 */

/**
 * @file	tensor_converter.c
 * @date	26 Mar 2018
 * @brief	GStreamer plugin to convert media types to tensors (as a filter for other general neural network filters)
 * @see		https://github.com/nnsuite/nnstreamer
 * @author	MyungJoo Ham <myungjoo.ham@samsung.com>
 * @bug		No known bugs except for NYI items
 */

/**
 *  @mainpage nnstreamer
 *  @section  intro         Introduction
 *  - Introduction      :   Neural Network Streamer for AI Projects
 *  @section   Program      Program Name
 *  - Program Name      :   nnstreamer
 *  - Program Details   :   It provides a neural network framework connectivities (e.g., tensorflow, caffe) for gstreamer streams.
 *    Efficient Streaming for AI Projects: Neural network models wanted to use efficient and flexible streaming management as well.
 *    Intelligent Media Filters!: Use a neural network model as a media filter / converter.
 *    Composite Models!: Allow to use multiple neural network models in a single stream instance.
 *    Multi Model Intelligence!: Allow to use multiple sources for neural network models.
 *  @section  INOUTPUT      Input/output data
 *  - INPUT             :   None
 *  - OUTPUT            :   None
 *  @section  CREATEINFO    Code information
 *  - Initial date      :   2018/06/14
 *  - Version           :   0.1
 */

/**
 * SECTION:element-tensor_converter
 *
 * A filter that converts media stream to tensor stream for NN frameworks.
 * The output is always in the format of other/tensor.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 videotestsrc ! video/x-raw,format=RGB,width=640,height=480 ! tensor_converter ! tensor_sink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "tensor_converter.h"

/**
 * @brief Macro for debug mode.
 */
#ifndef DBG
#define DBG (!self->silent)
#endif

/**
 * @brief Macro for debug message.
 */
#define silent_debug(...) \
    debug_print (DBG, __VA_ARGS__)

#define silent_debug_caps(caps,msg) do { \
  if (DBG) { \
    if (caps) { \
      GstStructure *caps_s; \
      gchar *caps_s_string; \
      guint caps_size, caps_idx; \
      caps_size = gst_caps_get_size (caps);\
      for (caps_idx = 0; caps_idx < caps_size; caps_idx++) { \
        caps_s = gst_caps_get_structure (caps, caps_idx); \
        caps_s_string = gst_structure_to_string (caps_s); \
        debug_print (TRUE, msg " = %s\n", caps_s_string); \
        g_free (caps_s_string); \
      } \
    } \
  } \
} while (0)

GST_DEBUG_CATEGORY_STATIC (gst_tensor_converter_debug);
#define GST_CAT_DEFAULT gst_tensor_converter_debug

/**
 * @brief tensor_converter properties
 */
enum
{
  PROP_0,
  PROP_INPUT_DIMENSION,
  PROP_INPUT_TYPE,
  PROP_FRAMES_PER_TENSOR,
  PROP_SILENT
};

/**
 * @brief Flag to print minimized log.
 */
#define DEFAULT_SILENT TRUE

/**
 * @brief Frames in output tensor.
 */
#define DEFAULT_FRAMES_PER_TENSOR 1

/**
 * @brief Template for sink pad.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_TENSOR_MEDIA_CAPS_STR));

/**
 * @brief Template for src pad.
 */
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_TENSOR_CAP_DEFAULT));

#define gst_tensor_converter_parent_class parent_class
G_DEFINE_TYPE (GstTensorConverter, gst_tensor_converter, GST_TYPE_ELEMENT);

static void gst_tensor_converter_finalize (GObject * object);
static void gst_tensor_converter_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_tensor_converter_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_tensor_converter_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_tensor_converter_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static gboolean gst_tensor_converter_src_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static GstFlowReturn gst_tensor_converter_chain (GstPad * pad,
    GstObject * parent, GstBuffer * buf);
static GstStateChangeReturn
gst_tensor_converter_change_state (GstElement * element,
    GstStateChange transition);

static void gst_tensor_converter_reset (GstTensorConverter * self);
static GstCaps *gst_tensor_converter_query_caps (GstTensorConverter * self,
    GstPad * pad, GstCaps * filter);
static gboolean gst_tensor_converter_parse_caps (GstTensorConverter * self,
    const GstCaps * caps);

/**
 * @brief Initialize the tensor_converter's class.
 */
static void
gst_tensor_converter_class_init (GstTensorConverterClass * klass)
{
  GObjectClass *object_class;
  GstElementClass *element_class;

  object_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;

  object_class->set_property = gst_tensor_converter_set_property;
  object_class->get_property = gst_tensor_converter_get_property;
  object_class->finalize = gst_tensor_converter_finalize;

  /**
   * GstTensorConverter::input-dim:
   *
   * Input tensor dimension from inner array.
   * Generally this property is used to set tensor configuration for byte-stream (application/octet-stream).
   * When setting this property and input media type is video or audio stream, GstTensorConverter will compare the media info with this.
   * (If it is different, it will be failed.)
   */
  g_object_class_install_property (object_class, PROP_INPUT_DIMENSION,
      g_param_spec_string ("input-dim", "Input tensor dimension",
          "Input tensor dimension from inner array", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstTensorConverter::input-type:
   *
   * Type of each element of the input tensor.
   * Generally this property is used to set tensor configuration for byte-stream (application/octet-stream).
   * When setting this property and input media type is video or audio stream, GstTensorConverter will compare the media info with this.
   * (If it is different, it will be failed.)
   */
  g_object_class_install_property (object_class, PROP_INPUT_TYPE,
      g_param_spec_string ("input-type", "Input tensor type",
          "Type of each element of the input tensor", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstTensorConverter::frames-per-tensor:
   *
   * The number of frames in outgoing buffer. (buffer is a sinle tensor instance)
   * GstTensorConverter can push a buffer with multiple media frames.
   */
  g_object_class_install_property (object_class, PROP_FRAMES_PER_TENSOR,
      g_param_spec_uint ("frames-per-tensor", "Frames per tensor",
          "The number of frames in output tensor", 1, G_MAXUINT,
          DEFAULT_FRAMES_PER_TENSOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstTensorConverter::silent:
   *
   * The flag to enable/disable debugging messages.
   */
  g_object_class_install_property (object_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce verbose output",
          DEFAULT_SILENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "TensorConverter",
      "Converter/Tensor",
      "Converts audio or video stream to tensor stream of C-Array for neural network framework filters",
      "MyungJoo Ham <myungjoo.ham@samsung.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));

  element_class->change_state = gst_tensor_converter_change_state;
}

/**
 * @brief Initialize tensor_converter element.
 */
static void
gst_tensor_converter_init (GstTensorConverter * self)
{
  /** setup sink pad */
  self->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_event_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tensor_converter_sink_event));
  gst_pad_set_query_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tensor_converter_sink_query));
  gst_pad_set_chain_function (self->sinkpad,
      GST_DEBUG_FUNCPTR (gst_tensor_converter_chain));
  GST_PAD_SET_PROXY_CAPS (self->sinkpad);
  gst_element_add_pad (GST_ELEMENT (self), self->sinkpad);

  /** setup src pad */
  self->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR (gst_tensor_converter_src_query));
  GST_PAD_SET_PROXY_CAPS (self->srcpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);

  /** init properties */
  self->silent = DEFAULT_SILENT;
  self->frames_per_tensor = DEFAULT_FRAMES_PER_TENSOR;
  self->in_media_type = _NNS_MEDIA_END;
  self->remove_padding = FALSE;
  gst_tensor_info_init (&self->tensor_info);

  self->adapter = gst_adapter_new ();
  gst_tensor_converter_reset (self);
}

/**
 * @brief Function to finalize instance.
 */
static void
gst_tensor_converter_finalize (GObject * object)
{
  GstTensorConverter *self;

  self = GST_TENSOR_CONVERTER (object);

  gst_tensor_converter_reset (self);

  if (self->adapter) {
    g_object_unref (self->adapter);
    self->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * @brief Setter for tensor_converter properties.
 */
static void
gst_tensor_converter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTensorConverter *self;

  self = GST_TENSOR_CONVERTER (object);

  switch (prop_id) {
    case PROP_INPUT_DIMENSION:
      g_assert (get_tensor_dimension (g_value_get_string (value),
              self->tensor_info.dimension) > 0);
      break;
    case PROP_INPUT_TYPE:
      self->tensor_info.type = get_tensor_type (g_value_get_string (value));
      g_assert (self->tensor_info.type != _NNS_END);
      break;
    case PROP_FRAMES_PER_TENSOR:
      self->frames_per_tensor = g_value_get_uint (value);
      silent_debug ("Set frames in output = %d", self->frames_per_tensor);
      break;
    case PROP_SILENT:
      self->silent = g_value_get_boolean (value);
      silent_debug ("Set silent = %d", self->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * @brief Getter for tensor_converter properties.
 */
static void
gst_tensor_converter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTensorConverter *self;

  self = GST_TENSOR_CONVERTER (object);

  switch (prop_id) {
    case PROP_INPUT_DIMENSION:
    {
      gchar *str_dim;

      str_dim = get_tensor_dimension_string (self->tensor_info.dimension);
      g_value_set_string (value, str_dim);
      g_free (str_dim);
      break;
    }
    case PROP_INPUT_TYPE:
      g_value_set_string (value,
          tensor_element_typename[self->tensor_info.type]);
      break;
    case PROP_FRAMES_PER_TENSOR:
      g_value_set_uint (value, self->frames_per_tensor);
      break;
    case PROP_SILENT:
      g_value_set_boolean (value, self->silent);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * @brief This function handles sink event.
 */
static gboolean
gst_tensor_converter_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GstTensorConverter *self;

  self = GST_TENSOR_CONVERTER (parent);

  GST_LOG_OBJECT (self, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *in_caps;
      GstCaps *out_caps;

      gst_event_parse_caps (event, &in_caps);
      silent_debug_caps (in_caps, "in-caps");

      if (gst_tensor_converter_parse_caps (self, in_caps)) {
        out_caps = gst_tensor_caps_from_config (&self->tensor_config);
        silent_debug_caps (out_caps, "out-caps");

        gst_pad_set_caps (self->srcpad, out_caps);
        gst_pad_push_event (self->srcpad, gst_event_new_caps (out_caps));
        gst_caps_unref (out_caps);
        return TRUE;
      }
      break;
    }
    case GST_EVENT_FLUSH_STOP:
      gst_tensor_converter_reset (self);
      break;
    default:
      break;
  }

  return gst_pad_event_default (pad, parent, event);
}

/**
 * @brief This function handles sink pad query.
 */
static gboolean
gst_tensor_converter_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstTensorConverter *self;

  self = GST_TENSOR_CONVERTER (parent);

  GST_LOG_OBJECT (self, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps;
      GstCaps *filter;

      gst_query_parse_caps (query, &filter);
      caps = gst_tensor_converter_query_caps (self, pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      GstCaps *template_caps;
      gboolean res;

      gst_query_parse_accept_caps (query, &caps);
      silent_debug_caps (caps, "accept-caps");

      template_caps = gst_pad_get_pad_template_caps (pad);
      res = gst_caps_can_intersect (template_caps, caps);

      gst_query_set_accept_caps_result (query, res);
      gst_caps_unref (template_caps);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

/**
 * @brief This function handles src pad query.
 */
static gboolean
gst_tensor_converter_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstTensorConverter *self;

  self = GST_TENSOR_CONVERTER (parent);

  GST_LOG_OBJECT (self, "Received %s query: %" GST_PTR_FORMAT,
      GST_QUERY_TYPE_NAME (query), query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *caps;
      GstCaps *filter;

      gst_query_parse_caps (query, &filter);
      caps = gst_tensor_converter_query_caps (self, pad, filter);

      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      return TRUE;
    }
    default:
      break;
  }

  return gst_pad_query_default (pad, parent, query);
}

/**
 * @brief Chain function, this function does the actual processing.
 */
static GstFlowReturn
gst_tensor_converter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstTensorConverter *self;
  GstTensorConfig *config;
  GstAdapter *adapter;
  GstBuffer *inbuf;
  gsize avail, buf_size, frame_size, out_size;
  guint frames_in, frames_out;
  GstFlowReturn ret = GST_FLOW_OK;

  buf_size = gst_buffer_get_size (buf);
  g_return_val_if_fail (buf_size > 0, GST_FLOW_ERROR);

  self = GST_TENSOR_CONVERTER (parent);

  g_assert (self->tensor_configured);
  config = &self->tensor_config;

  frames_out = self->frames_per_tensor;
  inbuf = buf;

  switch (self->in_media_type) {
    case _NNS_VIDEO:
    {
      guint color, width, height, type;

      color = config->info.dimension[0];
      width = config->info.dimension[1];
      height = config->info.dimension[2];
      type = tensor_element_size[config->info.type];

      /** colorspace * width * height * type */
      frame_size = color * width * height * type;

      /** supposed 1 frame in buffer */
      g_assert ((buf_size / GST_VIDEO_INFO_SIZE (&self->in_info.video)) == 1);
      frames_in = 1;

      if (self->remove_padding) {
        GstMapInfo src_info, dest_info;
        unsigned char *srcptr, *destptr;
        int d0, d1;
        unsigned int src_idx = 0, dest_idx = 0;
        size_t size, offset;

        inbuf = gst_buffer_new_and_alloc (frame_size);
        gst_buffer_memset (inbuf, 0, 0, frame_size);

        g_assert (gst_buffer_map (buf, &src_info, GST_MAP_READ));
        g_assert (gst_buffer_map (inbuf, &dest_info, GST_MAP_WRITE));

        srcptr = src_info.data;
        destptr = dest_info.data;

        /**
         * Refer: https://gstreamer.freedesktop.org/documentation/design/mediatype-video-raw.html
         */
        size = offset = color * width * type;

        g_assert (offset % 4);
        if (offset % 4) {
          offset += 4 - (offset % 4);
        }

        for (d0 = 0; d0 < frames_in; d0++) {
          for (d1 = 0; d1 < height; d1++) {
            memcpy (destptr + dest_idx, srcptr + src_idx, size);
            dest_idx += size;
            src_idx += offset;
          }
        }

        gst_buffer_unmap (buf, &src_info);
        gst_buffer_unmap (inbuf, &dest_info);

        /** copy timestamps */
        gst_buffer_copy_into (inbuf, buf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
      }
      break;
    }
    case _NNS_AUDIO:
      frame_size = GST_AUDIO_INFO_BPF (&self->in_info.audio);
      frames_in = buf_size / frame_size;
      break;

    case _NNS_STRING:
      frame_size = GST_TENSOR_STRING_SIZE;
      frames_in = 1; /** supposed 1 frame in buffer */

      if (buf_size != frame_size) {
        GstMapInfo src_info, dest_info;

        inbuf = gst_buffer_new_and_alloc (frame_size);
        gst_buffer_memset (inbuf, 0, 0, frame_size);

        g_assert (gst_buffer_map (buf, &src_info, GST_MAP_READ));
        g_assert (gst_buffer_map (inbuf, &dest_info, GST_MAP_WRITE));

        strncpy ((char *) dest_info.data, (char *) src_info.data,
            frame_size - 1);

        gst_buffer_unmap (buf, &src_info);
        gst_buffer_unmap (inbuf, &dest_info);

        /** copy timestamps */
        gst_buffer_copy_into (inbuf, buf, GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
      }
      break;

    case _NNS_OCTET:
      /** get frame size from the properties */
      frame_size = gst_tensor_info_get_size (&config->info);
      g_assert (frame_size > 0);
      g_assert ((buf_size % frame_size) == 0);
      frames_in = buf_size / frame_size;
      break;

    default:
      err_print ("Unsupported type %d\n", self->in_media_type);
      g_assert (0);
      return GST_FLOW_ERROR;
  }

  if (frames_in == frames_out) {
    /** do nothing, push the incoming buffer */
    return gst_pad_push (self->srcpad, inbuf);
  }

  adapter = self->adapter;
  g_assert (adapter != NULL);

  gst_adapter_push (adapter, inbuf);

  out_size = frames_out * frame_size;
  while ((avail = gst_adapter_available (adapter)) >= out_size &&
      ret == GST_FLOW_OK) {
    GstBuffer *outbuf;
    GstClockTime pts, dts;
    guint64 pts_dist, dts_dist;
    gsize offset;

    /** offset for last frame */
    offset = frame_size * (frames_out - 1) + 1; /** +1 byte */

    pts = gst_adapter_prev_pts_at_offset (adapter, offset, &pts_dist);
    dts = gst_adapter_prev_dts_at_offset (adapter, offset, &dts_dist);

    /**
     * Update timestamp of last frame.
     * If frames-in is larger then frames-out, the same timestamp (pts and dts) would be returned.
     */
    if (frames_in > 1) {
      gint fn, fd;

      fn = config->rate_n;
      fd = config->rate_d;

      if (fn > 0 && fd > 0) {
        if (GST_CLOCK_TIME_IS_VALID (pts)) {
          pts =
              gst_util_uint64_scale_int (pts_dist * fd, GST_SECOND,
              fn * frame_size);
        }

        if (GST_CLOCK_TIME_IS_VALID (dts)) {
          dts =
              gst_util_uint64_scale_int (dts_dist * fd, GST_SECOND,
              fn * frame_size);
        }
      }
    }

    outbuf = gst_adapter_take_buffer (adapter, out_size);
    outbuf = gst_buffer_make_writable (outbuf);

    /** set timestamp */
    GST_BUFFER_PTS (outbuf) = pts;
    GST_BUFFER_DTS (outbuf) = dts;

    ret = gst_pad_push (self->srcpad, outbuf);
  }

  return ret;
}

/**
 * @brief Called to perform state change.
 */
static GstStateChangeReturn
gst_tensor_converter_change_state (GstElement * element,
    GstStateChange transition)
{
  GstTensorConverter *self;
  GstStateChangeReturn ret;

  self = GST_TENSOR_CONVERTER (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_tensor_converter_reset (self);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_tensor_converter_reset (self);
      break;
    default:
      break;
  }

  return ret;
}

/**
 * @brief Clear and reset data.
 */
static void
gst_tensor_converter_reset (GstTensorConverter * self)
{
  if (self->adapter) {
    gst_adapter_clear (self->adapter);
  }

  self->tensor_configured = FALSE;
  gst_tensor_config_init (&self->tensor_config);
}

/**
 * @brief Get pad caps for caps negotiation.
 */
static GstCaps *
gst_tensor_converter_query_caps (GstTensorConverter * self, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *caps;

  if (pad == self->sinkpad) {
    caps = gst_pad_get_pad_template_caps (pad);
  } else {
    caps = gst_tensor_caps_from_config (&self->tensor_config);
  }

  silent_debug_caps (caps, "caps");
  silent_debug_caps (filter, "filter");

  if (caps && filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (caps);
    caps = intersection;
  }

  return caps;
}

/**
 * @brief Parse caps and set tensor info.
 */
static gboolean
gst_tensor_converter_parse_caps (GstTensorConverter * self,
    const GstCaps * caps)
{
  GstStructure *structure;
  GstTensorConfig config;
  media_type in_type;
  gint frames_dim = -1; /** dimension index of frames in configured tensor */

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (gst_caps_is_fixed (caps), FALSE);

  structure = gst_caps_get_structure (caps, 0);
  in_type = gst_tensor_media_type_from_structure (structure);

  if (!gst_tensor_config_from_structure (&config, structure)) {
    /** cannot configure tensor */
    return FALSE;
  }

  switch (in_type) {
    case _NNS_VIDEO:
    {
      GstVideoInfo info;

      gst_video_info_init (&info);
      if (!gst_video_info_from_caps (&info, caps)) {
        err_print ("Failed to get video info from caps.\n");
        return FALSE;
      }

      /**
       * Emit Warning if RSTRIDE = RU4 (3BPP) && Width % 4 > 0
       * @todo Add more conditions!
       */
      if (gst_tensor_video_stride_padding_per_row (GST_VIDEO_INFO_FORMAT
              (&info), GST_VIDEO_INFO_WIDTH (&info))) {
        self->remove_padding = TRUE;
        silent_debug ("Set flag to remove padding, width = %d",
            GST_VIDEO_INFO_WIDTH (&info));

        GST_WARNING_OBJECT (self,
            "\nYOUR STREAM CONFIGURATION INCURS PERFORMANCE DETERIORATION!\nPlease use 4 x n as image width for inputs.\n");
      }

      frames_dim = 3;
      self->in_info.video = info;
      break;
    }
    case _NNS_AUDIO:
    {
      GstAudioInfo info;

      gst_audio_info_init (&info);
      if (!gst_audio_info_from_caps (&info, caps)) {
        err_print ("Failed to get audio info from caps.\n");
        return FALSE;
      }

      frames_dim = 1;
      self->in_info.audio = info;
      break;
    }
    case _NNS_STRING:
      frames_dim = 1;
      break;
    case _NNS_OCTET:
      /**
       * update tensor info from properties
       */
      if (!gst_tensor_info_validate (&self->tensor_info)) {
        err_print ("Failed to get tensor info, update dimension and type.\n");
        return FALSE;
      }

      config.info = self->tensor_info;
      break;
    default:
      err_print ("Unsupported type %d\n", in_type);
      return FALSE;
  }

  /** set the number of frames in dimension */
  if (frames_dim >= 0) {
    config.info.dimension[frames_dim] = self->frames_per_tensor;
  }

  if (!gst_tensor_config_validate (&config)) {
    /** not fully configured */
    err_print ("Failed to configure tensor info.\n");
    return FALSE;
  }

  if (gst_tensor_info_validate (&self->tensor_info)) {
    /** compare tensor info */
    if (!gst_tensor_info_is_equal (&self->tensor_info, &config.info)) {
      err_print ("Failed, mismatched tensor info.\n");
      return FALSE;
    }
  }

  self->in_media_type = in_type;
  self->tensor_configured = TRUE;
  self->tensor_config = config;
  self->tensor_info = config.info;
  return TRUE;
}

/**
 * @brief Function to initialize the plugin.
 *
 * See GstPluginInitFunc() for more details.
 */
NNSTREAMER_PLUGIN_INIT (tensor_converter)
{
  GST_DEBUG_CATEGORY_INIT (gst_tensor_converter_debug, "tensor_converter",
      0, "tensor_converter element");

  return gst_element_register (plugin, "tensor_converter",
      GST_RANK_NONE, GST_TYPE_TENSOR_CONVERTER);
}

/**
 * @brief Definition for identifying tensor_converter plugin.
 *
 * PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "tensor_converter"
#endif

#ifndef SINGLE_BINARY
/**
 * @brief Macro to define the entry point of the plugin.
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    tensor_converter,
    "GStreamer plugin to convert media types to tensors",
    gst_tensor_converter_plugin_init, VERSION, "LGPL", "GStreamer",
    "http://github.com/nnsuite/nnstreamer/");
#endif
