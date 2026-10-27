#pragma once
#include "stdafx.h"
#include "CamBase.h"

// include math defines for M_PI
#include <corecrt_math_defines.h>
// hint from: https://discourse.mcneel.com/t/multithreading-in-rhino-c-plug-in/76122/5
#include <boost/thread/win32/mfc_thread_init.hpp>
//libslic3r includes
#include <Model.hpp>
#include <Print.hpp>

class CamSlicer : public Aion::CamBase
{
public:
    // initialize with default name
    CamSlicer(const ON_UUID& plugin_uuid);
    ~CamSlicer() = default;
    // override class information
    // name of this cam block type
    virtual std::string getTypeName() const override { return "Slicer"; };
    // short description of this cam block type
    virtual std::string getTypeShortDescription() const override { return "Slice a volumetric object"; }
    // Optional url to documentation for this cam block type.
    virtual std::string getDocumentationUrl() const override { return "https://documentation.kronos-mct.com/aion/latest/cam/volume/"; }
    // factory method to create a new instance of this cam block type
    virtual Aion::CamBase* createNewInstance() const override;

    // generate Toolpath of the contour CAM block
    bool generateToolpath(Aion::Errorlist& errors) override;
	// Get the icon for the CAM block
	// virtual HICON getIcon(int size_x, int size_y) const override;
    // Expose options of this cam block to UI
    Aion::OptionsGroups getOptionsByGroup() override;
    // callback for when an option was changed in the UI.
    void onOptionChanged(const Aion::ConfigOptionBase* option, bool reset = false) override;
    // Respond to modified geometry in the document
    void onObjectChanged(const ON_UUID& uuid, ON::object_type type, bool deleted) override;

    // Custom methods for the libslic3r integration
	// create a ON_Mesh of a given Component
	std::unique_ptr<ON_Mesh> createNurbsMesh(Aion::Component& comp, ON_MeshParameters mesh_params = ON_MeshParameters::DefaultMesh);

	// create a Slic3r::TriangleMesh of a given ON_Mesh
	std::unique_ptr<Slic3r::TriangleMesh> createSlicerMesh(const ON_Mesh* on_mesh);

	bool isExtrusionPath(const Slic3r::ExtrusionEntity* e) { return dynamic_cast<const Slic3r::ExtrusionPath*>(e) != nullptr; }
	bool isExtrusionMultiPath(const Slic3r::ExtrusionEntity* e) { return dynamic_cast<const Slic3r::ExtrusionMultiPath*>(e) != nullptr; }
	bool isExtrusionLoop(const Slic3r::ExtrusionEntity* e) { return e->is_loop(); }

	// convert and add Slic3r::ExtrusionPath the ToolpathPath
	void createToolpathForSlicerpath(Aion::ToolpathPath* const path_ptr, const Slic3r::ExtrusionPath* s_path_ptr, double path_z, const ON_Xform& transform, const ON_3dVector& dir, const Slic3r::DynamicPrintConfig& s_config);

	double getSpeedForExtrusionRole(Slic3r::ExtrusionRole& s_role, bool is_first_layer, const Slic3r::DynamicPrintConfig& s_config);

	void createFlatCollectionFromLayer(const Slic3r::Layer* s_layer, Slic3r::ExtrusionEntityCollection& collection);

	void processEntityCollection(const Slic3r::ExtrusionEntityCollection& s_collection, double path_z, const Aion::ToolConfig* tool_config, const ON_Xform& transform, const ON_3dVector& dir, const Slic3r::DynamicPrintConfig& s_config);

private:
	// Components of the volume structure object in the Rhino scene
	Aion::ConfigOptionComponentVector* m_volume_components;
	// Slicing direction (build direction) of the volumes
	Aion::ConfigOptionDirection* m_orientation_vector;
	//
	Aion::ConfigOptionEnum* m_slicer_config_mode;
	//
	Aion::ConfigOptionString* m_slicer_config_path;
	//
	Aion::ConfigOptionString* m_last_slicer_config_path;
	// 
	Aion::ConfigOptionString* m_slicer_config_string;
	// 
	Aion::ConfigOptionString* m_last_slicer_config_string;
	// Config option for the tool
	Aion::ConfigOptionTool* m_tool;
	// Config option to save the last used tool (not in the UI)
	Aion::ConfigOptionTool* m_last_tool;

	Aion::ConfigOptionBool* m_support;
	Aion::ConfigOptionBool* m_brim;
	Aion::ConfigOptionBool* m_skirt;
	// small epsilon to compare if points are equal.
    double slicerEPS = 0.0001;
};