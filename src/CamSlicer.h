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

	bool isExtrusionPath(const Slic3r::ExtrusionEntity* e) { return dynamic_cast<const Slic3r::ExtrusionPath*>(e) != nullptr; }
	bool isExtrusionMultiPath(const Slic3r::ExtrusionEntity* e) { return dynamic_cast<const Slic3r::ExtrusionMultiPath*>(e) != nullptr; }
	bool isExtrusionLoop(const Slic3r::ExtrusionEntity* e) { return e->is_loop(); }

private:
    // Apply the options of this cam block to the slicer config
    // @param s_config: reference to the slicer config to apply the options to
	void applyConfigOptionsToSlicerConfig(Slic3r::DynamicPrintConfig& s_config);
	// build the slicer config: PrusaSlicer defaults < baseline .ini < cam block options < user config options
    // @param s_config: reference to the slicer config to build
    // @param errors: reference to error list to append errors/warnings/info messages
    // @return true if the slicer config was built successfully, false otherwise
	bool loadSlicerConfig(Slic3r::DynamicPrintConfig& s_config, Aion::Errorlist& errors);
	// Get the speed of an extrusion role from the print speed and the speed factors of this cam block.
	// The slicer only computes speeds during its gcode export, which is not used, so the speeds are not taken from the slicer config.
	// @param s_role: Slic3r ExtrusionRole to get the speed for
	// @param is_small_perimeter: whether the path belongs to a perimeter not longer than SMALL_PERIMETER_LENGTH
	// @param is_first_layer: whether the path is on the first layer, the first layer speed overrides all other speeds
	// @return the speed for the given extrusion role in mm/s
	double getSpeedForExtrusionRole(Slic3r::ExtrusionRole s_role, bool is_small_perimeter, bool is_first_layer) const;

	// Append the points of a Slic3r ExtrusionPath to a ToolpathPath
	// @param path_ptr: pointer to the ToolpathPath to fill
	// @param s_path_ptr: pointer to the Slic3r ExtrusionPath to convert
	// @param path_z: Z height of the path
	// @param transform: transformation to apply to the points
	// @param dir: direction vector to apply to the points
	// @param is_small_perimeter: whether the path belongs to a perimeter not longer than SMALL_PERIMETER_LENGTH
	void createToolpathForSlicerpath(Aion::ToolpathPath* const path_ptr, const Slic3r::ExtrusionPath* s_path_ptr, double path_z, const ON_Xform& transform, const ON_3dVector& dir, bool is_small_perimeter) const;

	// convert each extrusion entity (path, multipath or loop) of a flat collection into one ToolpathPath with wiping at start and end.
	// path_z is the layer height in slicer coordinates, transform maps slicer coordinates back into the Rhino scene.
	// if a path does not start where the previous toolpath ended, a ToolpathTravel is inserted that lifts 1 mm along
	// the tool direction, moves above the start of the next path and approaches it. The results are appended to m_toolpaths.
	void processEntityCollection(const Slic3r::ExtrusionEntityCollection& s_collection, double path_z, const Aion::ToolConfig* tool_config, const ON_Xform& transform, const ON_3dVector& dir);

	// collect all extrusion entities of a layer into one flat collection (no nested collections).
	// support layers contribute their support fills, object layers the perimeters followed by the fills of each region.
	void createFlatCollectionFromLayer(const Slic3r::Layer* s_layer, Slic3r::ExtrusionEntityCollection& collection);

private:
	// Components of the volume structure object in the Rhino scene
	Aion::ConfigOptionComponentVector* m_volume_components;
	// Slicing direction (build direction) of the volumes
	Aion::ConfigOptionDirection* m_orientation_vector;
	// Config option for the tool
	Aion::ConfigOptionTool* m_tool;
	// Config option to save the last used tool (not in the UI)
	Aion::ConfigOptionTool* m_last_tool;

	// Extrusion parameters
	Aion::ConfigOptionFloat* m_width;
	Aion::ConfigOptionFloat* m_height;
	Aion::ConfigOptionFloat* m_first_layer_height;
	// Base print speed
	Aion::ConfigOptionFloat* m_speed;
	// Container for the speeds per extrusion type in % of m_speed
	Aion::ConfigOptionContainer* m_speed_factors;
	Aion::ConfigOptionPercent* m_perimeter_speed;
	Aion::ConfigOptionPercent* m_small_perimeter_speed;
	Aion::ConfigOptionPercent* m_external_perimeter_speed;
	Aion::ConfigOptionPercent* m_infill_speed;
	Aion::ConfigOptionPercent* m_solid_infill_speed;
	Aion::ConfigOptionPercent* m_top_solid_infill_speed;
	Aion::ConfigOptionPercent* m_support_speed;
	Aion::ConfigOptionPercent* m_support_interface_speed;
	Aion::ConfigOptionPercent* m_bridge_speed;
	Aion::ConfigOptionPercent* m_over_bridge_speed;
	Aion::ConfigOptionPercent* m_gap_fill_speed;
	Aion::ConfigOptionPercent* m_first_layer_speed;

	// Filling options
	Aion::ConfigOptionInt* m_perimeters;
	Aion::ConfigOptionEnum* m_perimeter_generator;
	Aion::ConfigOptionBool* m_vase_mode;
	Aion::ConfigOptionInt* m_top_layers;
	Aion::ConfigOptionInt* m_bottom_layers;
	Aion::ConfigOptionPercent* m_infill_density;
	Aion::ConfigOptionEnum* m_infill_pattern;

	// Other
	Aion::ConfigOptionBool* m_support;
	Aion::ConfigOptionFloat* m_brim_width;
	// Additional PrusaSlicer options in .ini format, applied last
	Aion::ConfigOptionString* m_slicer_options;
	// small epsilon to compare if points are equal.
    double slicerEPS = 0.0001;
};