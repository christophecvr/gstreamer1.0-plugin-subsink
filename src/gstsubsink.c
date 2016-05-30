/* GStreamer
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or(at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 59 Temple Place - Suite 330,
* Boston, MA 02111-1307, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include <gst/gstbuffer.h>
#include <gst/gstbufferlist.h>

#include <string.h>

#include "gstsubsink-marshal.h"
#include "gstsubsink.h"

struct _GstSubSinkPrivate
{
	GstCaps *caps;

	gboolean flushing;

	gpointer user_data;
	GDestroyNotify notify;
};

GST_DEBUG_CATEGORY_STATIC(sub_sink_debug);
#define GST_CAT_DEFAULT sub_sink_debug

enum
{
	/* signals */
	SIGNAL_NEW_BUFFER,

	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_CAPS,
	PROP_LAST
};

static GstStaticPadTemplate gst_sub_sink_template =
GST_STATIC_PAD_TEMPLATE("sink",
		GST_PAD_SINK,
		GST_PAD_ALWAYS,
		GST_STATIC_CAPS_ANY);
static GstBaseSinkClass *parent_class = NULL;

static void gst_sub_sink_uri_handler_init(gpointer g_iface,
		gpointer iface_data);

static void gst_sub_sink_init(GstSubSink *subsink);
static void gst_sub_sink_dispose(GObject *object);

static void gst_sub_sink_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_sub_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static gboolean gst_sub_sink_start(GstBaseSink *psink);
static gboolean gst_sub_sink_stop(GstBaseSink *psink);
static GstFlowReturn gst_sub_sink_render_common(GstBaseSink *psink, GstBuffer *buffer, gboolean is_list);
static GstFlowReturn gst_sub_sink_render(GstBaseSink *psink, GstBuffer *buffer);
static GstFlowReturn gst_sub_sink_render_list(GstBaseSink *psink, GstBufferList *list);
static GstCaps *gst_sub_sink_getcaps(GstBaseSink *psink, GstCaps *filter);
static GstStateChangeReturn gst_sub_sink_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_sub_sink_change_event(GstBaseSink *sink, GstEvent *event);

static guint gst_sub_sink_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_CODE(GstSubSink, gst_sub_sink, GST_TYPE_BASE_SINK, G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, gst_sub_sink_uri_handler_init));

static void gst_sub_sink_class_init(GstSubSinkClass * klass)
{
	GObjectClass *gobject_class =(GObjectClass *) klass;
	GstBaseSinkClass *basesink_class =(GstBaseSinkClass *) klass;

	GstElementClass *element_class = (GstElementClass *)klass;
	parent_class = g_type_class_peek_parent(klass);

	GST_DEBUG_CATEGORY_INIT(sub_sink_debug, "subsink", 0, "subsink element");

	gst_element_class_set_static_metadata(element_class, "SubSink",
			"Generic/Sink", "Allow the application to get access to raw subtitle data",
			"PLi team");

	gst_element_class_add_pad_template(element_class,
			gst_static_pad_template_get(&gst_sub_sink_template));

	gobject_class->dispose = gst_sub_sink_dispose;

	gobject_class->set_property = gst_sub_sink_set_property;
	gobject_class->get_property = gst_sub_sink_get_property;

	g_object_class_install_property(gobject_class, PROP_CAPS,
			g_param_spec_boxed("caps", "Caps",
					"The allowed caps for the sink pad", GST_TYPE_CAPS,
					G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/**
	* GstSubSink::new-buffer:
	* @subsink: the subsink element that emited the signal
	*
	* Signal that a new buffer is available.
	*
	* This signal is emited from the steaming thread
	*
	*/
	gst_sub_sink_signals[SIGNAL_NEW_BUFFER] =
			g_signal_new("new-buffer", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
			G_STRUCT_OFFSET(GstSubSinkClass, new_buffer),
			NULL, NULL, gst_subsink_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);

	basesink_class->start = gst_sub_sink_start;
	basesink_class->stop = gst_sub_sink_stop;
	basesink_class->render = gst_sub_sink_render;
	basesink_class->render_list = gst_sub_sink_render_list;
	basesink_class->get_caps = gst_sub_sink_getcaps;
	basesink_class->event = gst_sub_sink_change_event;
	element_class->change_state = gst_sub_sink_change_state;
	g_type_class_add_private(klass, sizeof(GstSubSinkPrivate));
}

static void gst_sub_sink_init(GstSubSink *subsink)
{
	subsink->priv =
			G_TYPE_INSTANCE_GET_PRIVATE(subsink, GST_TYPE_SUB_SINK,
			GstSubSinkPrivate);
}

static void gst_sub_sink_dispose(GObject *obj)
{
	GstSubSink *subsink = GST_SUB_SINK_CAST(obj);
	GstSubSinkPrivate *priv = subsink->priv;

	GST_OBJECT_LOCK(subsink);
	if (priv->caps)
	{
		gst_caps_unref(priv->caps);
		priv->caps = NULL;
	}
	if (priv->notify) 
	{
		priv->notify(priv->user_data);
	}
	priv->user_data = NULL;
	priv->notify = NULL;

	GST_OBJECT_UNLOCK(subsink);

	G_OBJECT_CLASS(parent_class)->dispose(obj);
}

static void gst_sub_sink_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstSubSink *subsink = GST_SUB_SINK_CAST(object);

	switch(prop_id) 
	{
		case PROP_CAPS:
			gst_sub_sink_set_caps(subsink, gst_value_get_caps(value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static void gst_sub_sink_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstSubSink *subsink = GST_SUB_SINK_CAST(object);

	switch(prop_id) 
	{
		case PROP_CAPS:
		{
			GstCaps *caps;

			caps = gst_sub_sink_get_caps(subsink);
			gst_value_set_caps(value, caps);
			if (caps)
				gst_caps_unref(caps);
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}
}

static gboolean gst_sub_sink_start(GstBaseSink *psink)
{
	GstSubSink *subsink = GST_SUB_SINK_CAST(psink);
	GstSubSinkPrivate *priv = subsink->priv;

	GST_DEBUG_OBJECT(subsink, "starting");
	priv->flushing = FALSE;

	return TRUE;
}

static gboolean gst_sub_sink_stop(GstBaseSink *psink)
{
	GstSubSink *subsink = GST_SUB_SINK_CAST(psink);
	GstSubSinkPrivate *priv = subsink->priv;

	GST_DEBUG_OBJECT(subsink, "stopping");
	priv->flushing = TRUE;

	return TRUE;
}

static GstFlowReturn gst_sub_sink_render_common(GstBaseSink *psink, GstBuffer *buffer, gboolean is_list)
{
	GstSubSink *subsink = GST_SUB_SINK_CAST(psink);
	GstSubSinkPrivate *priv = subsink->priv;

	if (priv->flushing)
		goto flushing;

	g_signal_emit(subsink, gst_sub_sink_signals[SIGNAL_NEW_BUFFER], 0, gst_buffer_ref(buffer));

	return GST_FLOW_OK;

flushing:
	{
		GST_DEBUG_OBJECT(subsink, "we are flushing");
		return GST_FLOW_FLUSHING;
	}
}

static GstFlowReturn gst_sub_sink_render(GstBaseSink *psink, GstBuffer *buffer)
{
	return gst_sub_sink_render_common(psink, buffer, FALSE);
}

static GstFlowReturn gst_sub_sink_render_list(GstBaseSink *sink, GstBufferList *list)
{
	GstBuffer *buffer;
	guint i, len;
	GstFlowReturn flow;

	/* The application doesn't support buffer lists, extract individual buffers
	* then and push them one-by-one */
	GST_INFO_OBJECT(sink, "chaining each group in list as a merged buffer");

	len = gst_buffer_list_length(list);
	flow = GST_FLOW_OK;
	for (i = 0; i < len; i++)
	{
		buffer = gst_buffer_list_get(list, i);
		flow = gst_sub_sink_render(sink, buffer);
		if (flow != GST_FLOW_OK)
		{
			break;
		}
	}

	return flow;
}

static GstCaps *gst_sub_sink_getcaps(GstBaseSink *psink, GstCaps *filter)
{
	GstCaps *caps;
	GstSubSink *subsink = GST_SUB_SINK_CAST(psink);
	GstSubSinkPrivate *priv = subsink->priv;

	GST_OBJECT_LOCK(subsink);
	if ((caps = priv->caps))
	{
		if (filter)
		{
			caps = gst_caps_intersect_full(filter, caps, GST_CAPS_INTERSECT_FIRST);
		}
		else
		{
			gst_caps_ref(caps);
		}
	}
	GST_DEBUG_OBJECT(subsink, "got caps %" GST_PTR_FORMAT, caps);
	GST_OBJECT_UNLOCK(subsink);

	return caps;
}

/* external API */

void gst_sub_sink_set_caps(GstSubSink *subsink, const GstCaps *caps)
{
	GstCaps *old;
	GstSubSinkPrivate *priv;

	g_return_if_fail(GST_IS_SUB_SINK(subsink));

	priv = subsink->priv;

	GST_OBJECT_LOCK(subsink);
	GST_DEBUG_OBJECT(subsink, "setting caps to %" GST_PTR_FORMAT, caps);
	if ((old = priv->caps) != caps) 
	{
		if (caps)
			priv->caps = gst_caps_copy(caps);
		else
			priv->caps = NULL;
		if (old)
			gst_caps_unref(old);
	}
	GST_OBJECT_UNLOCK(subsink);
}

GstCaps *gst_sub_sink_get_caps(GstSubSink *subsink)
{
	GstCaps *caps;
	GstSubSinkPrivate *priv;

	g_return_val_if_fail(GST_IS_SUB_SINK(subsink), NULL);

	priv = subsink->priv;

	GST_OBJECT_LOCK(subsink);
	if ((caps = priv->caps))
		gst_caps_ref(caps);
	GST_DEBUG_OBJECT(subsink, "getting caps of %" GST_PTR_FORMAT, caps);
	GST_OBJECT_UNLOCK(subsink);

	return caps;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType gst_sub_sink_uri_get_type(GType type)
{
	return GST_URI_SINK;
}

static const gchar *const *gst_sub_sink_uri_get_protocols(GType type)
{
	static const gchar *protocols[] = {"subsink", NULL};
	return protocols;
}

static gchar *gst_sub_sink_uri_get_uri(GstURIHandler *handler)
{
	return g_strdup("subsink");
}

static gboolean gst_sub_sink_uri_set_uri(GstURIHandler *handler, const gchar *uri, GError **error)
{
	/* GstURIHandler checks the protocol for us */
	return TRUE;
}

static void gst_sub_sink_uri_handler_init(gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface =(GstURIHandlerInterface *) g_iface;

	iface->get_type = gst_sub_sink_uri_get_type;
	iface->get_protocols = gst_sub_sink_uri_get_protocols;
	iface->get_uri = gst_sub_sink_uri_get_uri;
	iface->set_uri = gst_sub_sink_uri_set_uri;
}

/*** event capture CVR ***/
static gboolean gst_sub_sink_change_event(GstBaseSink *sink, GstEvent *event)
{
	GstSubSink *subsink = GST_SUB_SINK_CAST(sink);
	if(strncmp(gst_event_type_get_name(GST_EVENT_TYPE(event)), "gap", 3))
		GST_INFO_OBJECT(subsink, "EVENT %s", gst_event_type_get_name(GST_EVENT_TYPE(event)));
	else
		GST_DEBUG_OBJECT(subsink, "EVENT %s", gst_event_type_get_name(GST_EVENT_TYPE(event)));

	gboolean ret = TRUE;

	switch (GST_EVENT_TYPE(event))
	{
		case GST_EVENT_FLUSH_START:
			ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
			ret = TRUE;
			break;
		case GST_EVENT_FLUSH_STOP:
			ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
			ret = TRUE;
			break;
		case GST_EVENT_CAPS:
		{
			GstCaps *caps;
			gst_event_parse_caps(event, &caps);
			GST_INFO_OBJECT(subsink,"CAPS %"GST_PTR_FORMAT, caps);
			ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
			if (!ret)
			{
				gst_event_unref(event);
			}
		} break;
		case GST_EVENT_SEGMENT:
		{
			const GstSegment *segment;
			GstFormat format;
			gdouble rate;
			guint64 start, end, pos;
			gint64 start_dvb;
			gst_event_parse_segment(event, &segment);
			format = segment->format;
			rate = segment->rate;
			start = segment->start;
			end = segment->stop;
			pos = segment->position;
			start_dvb = start / 11111LL;
			GST_INFO_OBJECT(subsink, "SEGMENT rate=%f format=%d start=%"G_GUINT64_FORMAT
							 " pos=%"G_GUINT64_FORMAT
							 " end=%"G_GUINT64_FORMAT, rate, format, start, pos, end);
			GST_INFO_OBJECT(subsink, "SEGMENT DVB TIMESTAMP=%"G_GINT64_FORMAT
							 " HEXFORMAT %#"G_GINT64_MODIFIER "x", start_dvb, start_dvb);
			ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
			if (!ret)
			{
				gst_event_unref(event);
			}
		} break;
		case GST_EVENT_TAG:
		{
			GstTagList *taglist;
			gst_event_parse_tag(event, &taglist);
			GST_INFO_OBJECT(subsink,"TAG %"GST_PTR_FORMAT, taglist);
			ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
			if (!ret)
			{
				gst_event_unref(event);
			}
		} break;
		case GST_EVENT_TOC:
		{
			GstToc *toc;
			gboolean updated;
			gst_event_parse_toc(event, &toc, &updated);
			GList *toc_list = gst_toc_get_entries (toc);
			GST_INFO_OBJECT(subsink,"TOC %"GST_PTR_FORMAT, toc_list);
			gst_event_unref(event);
		} break;
		default:
			ret = GST_BASE_SINK_CLASS(parent_class)->event(sink, event);
			break;
	}
	return ret;
}

/*** state change CVR ****/

static GstStateChangeReturn gst_sub_sink_change_state(GstElement *element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	GstSubSink *subsink = GST_SUB_SINK_CAST(element);
	/* possible action to perform before the state change */
	switch(transition)
	{
		case GST_STATE_CHANGE_NULL_TO_READY:
			GST_INFO_OBJECT(subsink,"GST_STATE_CHANGE_NULL_TO_READY");
			break;
		case GST_STATE_CHANGE_READY_TO_PAUSED:
			GST_INFO_OBJECT(subsink,"GST_STATE_CHANGE_READY_TO_PAUSED");
			break;
		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			GST_INFO_OBJECT(subsink,"GST_STATE_CHANGE_PAUSED_TO_PLAYING");
			break;
		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);
	/* possible actions to perform after state change */
	switch(transition)
	{
		case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
			GST_INFO_OBJECT(subsink,"GST_STATE_CHANGE_PLAYING_TO_PAUSED");
			break;
		case GST_STATE_CHANGE_PAUSED_TO_READY:
			GST_INFO_OBJECT(subsink,"GST_STATE_CHANGE_PAUSED_TO_READY");
			break;
		case GST_STATE_CHANGE_READY_TO_NULL:
			GST_INFO_OBJECT(subsink,"GST_STATE_CHANGE_READY_TO_NULL");
			break;
		default:
			break;
	}

	return ret;
}

static gboolean plugin_init(GstPlugin *plugin)
{
	if (!gst_element_register(plugin, "subsink", GST_RANK_PRIMARY, GST_TYPE_SUB_SINK))
		return FALSE;

	return TRUE;
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR,
		GST_VERSION_MINOR,
		subsink,
		"delivers subtitle buffers to the application",
		plugin_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/");
