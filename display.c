/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "display.h"
#include "ovgosd.h"
#include "tools.h"
#include "setup.h"

#include <vdr/tools.h>

extern "C" {
#include "interface/vmcs_host/vc_tvservice.h"
#include "interface/vmcs_host/vc_dispmanx.h"
}

#include <bcm_host.h>

cRpiDisplay* cRpiDisplay::s_instance = 0;

cRpiDisplay* cRpiDisplay::GetInstance(void)
{
	if (!s_instance)
	{
		TV_DISPLAY_STATE_T tvstate;
		memset(&tvstate, 0, sizeof(TV_DISPLAY_STATE_T));

		if (!vc_tv_get_display_state(&tvstate))
		{
			// HDMI
			if (tvstate.state & (VC_HDMI_HDMI | VC_HDMI_DVI))
				s_instance = new cRpiHDMIDisplay(
						tvstate.display.hdmi.width,
						tvstate.display.hdmi.height,
						tvstate.display.hdmi.frame_rate,
						tvstate.display.hdmi.group,
						tvstate.display.hdmi.mode);
			else
				s_instance = new cRpiCompositeDisplay(
						tvstate.display.sdtv.width,
						tvstate.display.sdtv.height,
						tvstate.display.sdtv.frame_rate);
		}
		else
		{
			ELOG("failed to get display parameters - assuming analog SDTV!");
			s_instance = new cRpiCompositeDisplay(720, 576, 50);
		}
	}

	return s_instance;
}

void cRpiDisplay::DropInstance(void)
{
	SetSyncField(0);
	delete s_instance;
	s_instance = 0;
}

cRpiDisplay::cRpiDisplay(int width, int height, int frameRate,
		cRpiVideoPort::ePort port) :
	m_width(width),
	m_height(height),
	m_frameRate(frameRate),
	m_interlaced(false),
	m_port(port)
{
}

cRpiDisplay::~cRpiDisplay()
{
}

int cRpiDisplay::GetSize(int &width, int &height)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
	{
		width = instance->m_width;
		height = instance->m_height;
		return 0;
	}
	return -1;
}

int cRpiDisplay::GetSize(int &width, int &height, double &aspect)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
	{
		width = instance->m_width;
		height = instance->m_height;
		aspect = (double)width / height;
		return 0;
	}
	return -1;
}

int cRpiDisplay::SetVideoFormat(int width, int height, int frameRate,
		bool interlaced)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
		return instance->Update(width, height, frameRate, interlaced);

	return -1;
}

int cRpiDisplay::SetSyncField(int field)
{
	char response[64];
	return vc_gencmd(response, sizeof(response), "hvs_update_fields %d", field);
}

cRpiVideoPort::ePort cRpiDisplay::GetVideoPort(void)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
		return instance->m_port;

	return cRpiVideoPort::eComposite;
}

bool cRpiDisplay::IsProgressive(void)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
		return !instance->m_interlaced;

	return false;
}

int cRpiDisplay::Snapshot(unsigned char* frame, int width, int height)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
	{
		DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open(
				instance->m_port == cRpiVideoPort::eHDMI ? 0 : 1);

		if (display)
		{
			uint32_t image;
			DISPMANX_RESOURCE_HANDLE_T res = vc_dispmanx_resource_create(
					VC_IMAGE_RGB888, width, height, &image);

			vc_dispmanx_snapshot(display, res,
					(DISPMANX_TRANSFORM_T)(DISPMANX_SNAPSHOT_PACK));

			VC_RECT_T rect = { 0, 0, width, height };
			vc_dispmanx_resource_read_data(res, &rect, frame, width * 3);

			vc_dispmanx_resource_delete(res);
			vc_dispmanx_display_close(display);
			return 0;
		}
	}
	return -1;
}

int cRpiDisplay::Update(int width, int height, int frameRate, bool interlaced)
{
	if (cRpiSetup::GetVideoResolution() == cVideoResolution::eDontChange &&
				cRpiSetup::GetVideoFrameRate() == cVideoFrameRate::eDontChange)
		return 0;

	int newWidth = m_width;
	int newHeight = m_height;
	int newFrameRate = m_frameRate;
	bool newInterlaced = m_interlaced;

	switch (cRpiSetup::GetVideoResolution())
	{
	case cVideoResolution::e480:  newWidth = 720;  newHeight = 480;  break;
	case cVideoResolution::e576:  newWidth = 720;  newHeight = 576;  break;
	case cVideoResolution::e720:  newWidth = 1280; newHeight = 720;  break;
	case cVideoResolution::e1080: newWidth = 1920; newHeight = 1080; break;

	case cVideoResolution::eFollowVideo:
		if (width && height)
		{
			newWidth = width;
			newHeight = height;
		}
		break;

	default:
	case cVideoResolution::eDontChange:
		break;
	}

	switch (cRpiSetup::GetVideoFrameRate())
	{
	case cVideoFrameRate::e24p: newFrameRate = 24; newInterlaced = false; break;
	case cVideoFrameRate::e25p: newFrameRate = 25; newInterlaced = false; break;
	case cVideoFrameRate::e30p: newFrameRate = 30; newInterlaced = false; break;
	case cVideoFrameRate::e50i: newFrameRate = 50; newInterlaced = true;  break;
	case cVideoFrameRate::e50p: newFrameRate = 50; newInterlaced = false; break;
	case cVideoFrameRate::e60i: newFrameRate = 60; newInterlaced = true;  break;
	case cVideoFrameRate::e60p: newFrameRate = 60; newInterlaced = false; break;

	case cVideoFrameRate::eFollowVideo:
		if (frameRate)
		{
			newFrameRate = frameRate;
			newInterlaced = interlaced;
		}
		break;

	default:
	case cVideoFrameRate::eDontChange:
		break;
	}

	// set new mode only if necessary
	if (newWidth != m_width || newHeight != m_height ||
			newFrameRate != m_frameRate || newInterlaced != m_interlaced)
		return SetMode(newWidth, newHeight, newFrameRate, newInterlaced);

	return 0;
}

/* ------------------------------------------------------------------------- */

#define HDMI_MAX_MODES 64

class cRpiHDMIDisplay::ModeList {
public:

	TV_SUPPORTED_MODE_NEW_T modes[HDMI_MAX_MODES];
	int nModes;
};

cRpiHDMIDisplay::cRpiHDMIDisplay(int width, int height, int frameRate,
		int group, int mode) :
	cRpiDisplay(width, height, frameRate, cRpiVideoPort::eHDMI),
	m_modes(new cRpiHDMIDisplay::ModeList()),
	m_group(group),
	m_mode(mode),
	m_startGroup(group),
	m_startMode(mode),
	m_modified(false)
{
	vc_tv_register_callback(TvServiceCallback, 0);

	m_modes->nModes = vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_CEA,
			m_modes->modes, HDMI_MAX_MODES, NULL, NULL);

	m_modes->nModes += vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_DMT,
			&m_modes->modes[m_modes->nModes], HDMI_MAX_MODES - m_modes->nModes,
			NULL, NULL);

	if (m_modes->nModes)
	{
		DLOG("supported HDMI modes:");
		for (int i = 0; i < m_modes->nModes; i++)
		{
			DLOG("%s[%02d]: %4dx%4d@%2d%s | %s | %3d.%03dMHz%s%s",
				m_modes->modes[i].group == HDMI_RES_GROUP_CEA ? "CEA" :
				m_modes->modes[i].group == HDMI_RES_GROUP_DMT ? "DMT" : "---",
				m_modes->modes[i].code,
				m_modes->modes[i].width,
				m_modes->modes[i].height,
				m_modes->modes[i].frame_rate,
				m_modes->modes[i].scan_mode ? "i" : "p",
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_4_3   ? " 4:3 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_14_9  ? "14:9 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_16_9  ? "16:9 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_5_4   ? " 5:4 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_16_10 ? "16:10" :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_15_9  ? "15:9 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_21_9  ? "21:9 " :
					"unknown aspect ratio",
				m_modes->modes[i].pixel_freq / 1000000,
				m_modes->modes[i].pixel_freq % 1000000 / 1000,
				m_modes->modes[i].native ? " (native)" : "",
				m_modes->modes[i].code == m_mode &&
					m_modes->modes[i].group == m_group ? " (current)" : "");

			// update initial parameters
			if (m_modes->modes[i].code == m_mode &&
					m_modes->modes[i].group == m_group)
			{
				m_width = m_modes->modes[i].width;
				m_height = m_modes->modes[i].height;
				m_frameRate = m_modes->modes[i].frame_rate;
				m_interlaced = m_modes->modes[i].scan_mode;
			}
		}
	}
	else
		ELOG("failed to read HDMI EDID information!");

}

cRpiHDMIDisplay::~cRpiHDMIDisplay()
{
	vc_tv_unregister_callback(TvServiceCallback);

	// if mode has been changed, set back to previous state
	if (m_modified)
		SetMode(m_startGroup, m_startMode);

	delete m_modes;
}

int cRpiHDMIDisplay::SetMode(int width, int height, int frameRate,
		bool interlaced)
{
	for (int i = 0; i < m_modes->nModes; i++)
	{
		if (m_modes->modes[i].width == width &&
				m_modes->modes[i].height == height &&
				m_modes->modes[i].frame_rate == frameRate &&
				m_modes->modes[i].scan_mode == interlaced)
		{
			DLOG("setting HDMI mode to %dx%d@%2d%s", width, height,
					frameRate, interlaced ? "i" : "p");

			m_width = width;
			m_height = height;
			m_frameRate = frameRate;
			m_interlaced = interlaced;
			return SetMode(m_modes->modes[i].group, m_modes->modes[i].code);
		}
	}

	DLOG("failed to set HDMI mode to %dx%d@%2d%s",
		width, height, frameRate, interlaced ? "i" : "p");
	return -1;
}

int cRpiHDMIDisplay::SetMode(int group, int mode)
{
	int ret = 0;
	if (group != m_group || mode != m_mode)
	{
		DBG("cHDMI::SetMode(%s, %d)",
			group == HDMI_RES_GROUP_DMT ? "DMT" :
			group == HDMI_RES_GROUP_CEA ? "CEA" : "unknown", mode);

		ret = vc_tv_hdmi_power_on_explicit_new(HDMI_MODE_HDMI,
				(HDMI_RES_GROUP_T)group, mode);
		if (ret)
			ELOG("failed to set HDMI mode!");
		else
		{
			m_group = group;
			m_mode = mode;
			m_modified = true;
		}
	}
	return ret;
}

void cRpiHDMIDisplay::TvServiceCallback(void *data, unsigned int reason,
		unsigned int param1, unsigned int param2)
{
	if (reason & VC_HDMI_DVI + VC_HDMI_HDMI)
		cRpiOsdProvider::ResetOsd();
}

/* ------------------------------------------------------------------------- */

cRpiCompositeDisplay::cRpiCompositeDisplay(int width, int height,
		int frameRate) :
	cRpiDisplay(width, height, frameRate, cRpiVideoPort::eComposite)
{

}
