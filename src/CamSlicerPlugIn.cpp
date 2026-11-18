// CamSlicerPlugIn.cpp : defines the initialization routines for the plug-in.
//

#include "stdafx.h"
#include "rhinoSdkPlugInDeclare.h"
#include "CamSlicerPlugIn.h"
#include "Resource.h"
#include "CamSlicer.h"

// The plug-in object must be constructed before any plug-in classes derived
// from CRhinoCommand. The #pragma init_seg(lib) ensures that this happens.
#pragma warning(push)
#pragma warning(disable : 4073)
#pragma init_seg(lib)
#pragma warning(pop)

// Rhino plug-in declaration
RHINO_PLUG_IN_DECLARE

// Rhino plug-in name
// Provide a short, friendly name for this plug-in.
RHINO_PLUG_IN_NAME(VER_PROJECT_NAME_WSTR);

// Rhino plug-in id
// Provide a unique uuid for this plug-in.
// convert const char* to std::wstring
std::string plugin_uuid = VER_PLUGIN_UUID;
std::wstring VER_PLUGIN_UUID_w(plugin_uuid.begin(), plugin_uuid.end());
// set the plugin uuid from CMakeLists.txt (needed to have access to the uuid in cpack)
RHINO_PLUG_IN_ID(VER_PLUGIN_UUID_w.c_str());

// Rhino plug-in version
// Provide a version number string for this plug-in.
RHINO_PLUG_IN_VERSION(VER_FILEVERSION_STR);

// Rhino plug-in description
// Provide a description of this plug-in.
RHINO_PLUG_IN_DESCRIPTION(L"Volume CAM block plug-in for Aion-5X.");

// Rhino plug-in icon resource id
// Provide an icon resource this plug-in.
// Icon resource should contain 16, 24, 32, 48, and 256-pixel image sizes.
RHINO_PLUG_IN_ICON_RESOURCE_ID(IDI_ICON);

// Rhino plug-in developer declarations
// TODO: fill in the following developer declarations with
// your company information. Note, all of these declarations
// must be present or your plug-in will not load.
//
// When completed, delete the following #error directive.
//#error Developer declarations block is incomplete!
RHINO_PLUG_IN_DEVELOPER_ORGANIZATION(VER_VENDOR_WSTR);
RHINO_PLUG_IN_DEVELOPER_ADDRESS(L"Muggenhofer Straße 141\r\n90429 Nürnberg");
RHINO_PLUG_IN_DEVELOPER_COUNTRY(L"Germany");
RHINO_PLUG_IN_DEVELOPER_PHONE(L"");
RHINO_PLUG_IN_DEVELOPER_EMAIL(L"customerservice@kronos-mct.com");
RHINO_PLUG_IN_DEVELOPER_WEBSITE(VER_VENDOR_URL_WSTR);
RHINO_PLUG_IN_UPDATE_URL(VER_UPDATE_URL_WSTR);

// The one and only CCamSlicerPlugIn object
static class CCamSlicerPlugIn thePlugIn;

/////////////////////////////////////////////////////////////////////////////
// CCamSlicerPlugIn definition

CCamSlicerPlugIn& CamSlicerPlugIn()
{
	// Return a reference to the one and only CCamSlicerPlugIn object
	return thePlugIn;
}

CCamSlicerPlugIn::CCamSlicerPlugIn()
{
	// Description:
	//   CCamSlicerPlugIn constructor. The constructor is called when the
	//   plug-in is loaded and "thePlugIn" is constructed. Once the plug-in
	//   is loaded, CCamSlicerPlugIn::OnLoadPlugIn() is called. The
	//   constructor should be simple and solid. Do anything that might fail in
	//   CCamSlicerPlugIn::OnLoadPlugIn().

	// TODO: Add construction code here
	m_plugin_version = RhinoPlugInVersion();
}

/////////////////////////////////////////////////////////////////////////////
// Required overrides

const wchar_t* CCamSlicerPlugIn::PlugInName() const
{
	// Description:
	//   Plug-in name display string.  This name is displayed by Rhino when
	//   loading the plug-in, in the plug-in help menu, and in the Rhino
	//   interface for managing plug-ins.

	// TODO: Return a short, friendly name for the plug-in.
	return RhinoPlugInName();
}

const wchar_t* CCamSlicerPlugIn::PlugInVersion() const
{
	// Description:
	//   Plug-in version display string. This name is displayed by Rhino
	//   when loading the plug-in and in the Rhino interface for managing
	//   plug-ins.

	// TODO: Return the version number of the plug-in.
	return m_plugin_version;
}

GUID CCamSlicerPlugIn::PlugInID() const
{
	// Description:
	//   Plug-in unique identifier. The identifier is used by Rhino to
	//   manage the plug-ins.

	// TODO: Return a unique identifier for the plug-in.
	// {538245D1-E68C-4FC2-86B4-EAD3E1500C22}
	return ON_UuidFromString(RhinoPlugInId());
}

/////////////////////////////////////////////////////////////////////////////
// Additional overrides

BOOL CCamSlicerPlugIn::OnLoadPlugIn()
{
	// Description:
	//   Called after the plug-in is loaded and the constructor has been
	//   run. This is a good place to perform any significant initialization,
	//   license checking, and so on.  This function must return TRUE for
	//   the plug-in to continue to load.

	// Remarks:
	//    Plug-ins are not loaded until after Rhino is started and a default document
	//    is created.  Because the default document already exists
	//    CRhinoEventWatcher::On????Document() functions are not called for the default
	//    document.  If you need to do any document initialization/synchronization then
	//    override this function and do it here.  It is not necessary to call
	//    CPlugIn::OnLoadPlugIn() from your derived class.
	// TODO: Add plug-in initialization code here.

	registerAtAion5X();

	return TRUE;
}

void CCamSlicerPlugIn::OnUnloadPlugIn()
{
	// Description:
	//    Called one time when plug-in is about to be unloaded. By this time,
	//    Rhino's mainframe window has been destroyed, and some of the SDK
	//    managers have been deleted. There is also no active document or active
	//    view at this time. Thus, you should only be manipulating your own objects.
	//    or tools here.

	// TODO: Add plug-in cleanup code here.
}

std::vector<Aion::CamBase*> CCamSlicerPlugIn::getCamBlockTypes()
{
	std::vector<Aion::CamBase*> result;
	result.emplace_back(new CamSlicer(PlugInID()));
	return result;
}

