#include "stdafx.h"
#include <filesystem>
#include "CamSlicer.h"
#include "InteractiveSelection.h"
#include "Geometry5X.h"
#include <format.hpp>
#include <boost/property_tree/ini_parser.hpp>

// Baseline PrusaSlicer config, shipped next to the plugin .rhp
static const wchar_t* BASELINE_CONFIG_FILENAME = L"Aion_prusa_config.ini";

// Infill patterns: UI label -> PrusaSlicer "fill_pattern" key. The index is the enum value of the option.
static const std::vector<std::pair<std::string, std::string>> INFILL_PATTERNS{
    {"Rectilinear", "rectilinear"},
    {"Aligned Rectilinear", "alignedrectilinear"},
    {"Grid", "grid"},
    {"Triangles", "triangles"},
    {"Stars", "stars"},
    {"Cubic", "cubic"},
    {"Line", "line"},
    {"Concentric", "concentric"},
    {"Honeycomb", "honeycomb"},
    {"3D Honeycomb", "3dhoneycomb"},
    {"Gyroid", "gyroid"},
    {"Hilbert Curve", "hilbertcurve"},
    {"Archimedean Chords", "archimedeanchords"},
    {"Octagram Spiral", "octagramspiral"},
    {"Adaptive Cubic", "adaptivecubic"},
    {"Support Cubic", "supportcubic"},
    {"Lightning", "lightning"},
    {"Zig Zag", "zigzag"},
};
static const int DEFAULT_INFILL_PATTERN = 2; // Grid

// Helper function instead of static data to avoid delay-load issues from Aion-5X.rhp
inline const std::map<std::string, int>& GetInfillPatternStringMap() {
    static const std::map<std::string, int> map = [] {
        std::map<std::string, int> result;
        for (int i = 0; i < (int)INFILL_PATTERNS.size(); i++) {
            result[INFILL_PATTERNS[i].first] = i;
        }
        return result;
    }();
    return map;
}

enum PerimeterGenerator : unsigned char {
    arachne,
    classic
};

// Helper function instead of static data to avoid delay-load issues from Aion-5X.rhp
inline const std::map<std::string, int>& GetPerimeterGeneratorStringMap() {
    static const std::map<std::string, int> map{
        {"Arachne", arachne},
        {"Classic", classic}
    };
    return map;
}

enum SupportMode : unsigned char {
    supportNone,
    supportBottomOnly,
    supportEverywhere
};

// Helper function instead of static data to avoid delay-load issues from Aion-5X.rhp
inline const std::map<std::string, int>& GetSupportModeStringMap() {
    static const std::map<std::string, int> map{
        {"None", supportNone},
        {"Bottom only", supportBottomOnly},
        {"Everywhere", supportEverywhere}
    };
    return map;
}

// PrusaSlicer extrusion width keys, all set to the width of the CAM block
static const std::vector<const char*> EXTRUSION_WIDTH_KEYS{
    "extrusion_width",
    "first_layer_extrusion_width",
    "perimeter_extrusion_width",
    "external_perimeter_extrusion_width",
    "infill_extrusion_width",
    "solid_infill_extrusion_width",
    "top_infill_extrusion_width",
    "support_material_extrusion_width",
};

// PrusaSlicer options that only act in its G-code export, which isn't used. Grouped by the reason of the Info message,
// which lists the keys of a group that differ from the PrusaSlicer default (nullable filament overrides: that are not nil).
static const std::vector<std::pair<const char*, std::vector<const char*>>> UNSUPPORTED_OPTIONS{
    {"are ignored, the retraction and wiping of Aion's tool and the \"Travel Z hop\" option are used instead", {
        "wipe", "retract_length", "retract_speed", "deretract_speed", "retract_restart_extra", "retract_before_wipe",
        "retract_lift", "retract_lift_above", "retract_lift_below", "travel_ramping_lift", "travel_max_lift", "travel_slope",
        "travel_lift_before_obstacle", "use_firmware_retraction",
        "filament_wipe", "filament_retract_length", "filament_retract_speed", "filament_deretract_speed",
        "filament_retract_restart_extra", "filament_retract_before_wipe", "filament_retract_lift", "filament_retract_lift_above",
        "filament_retract_lift_below", "filament_travel_ramping_lift", "filament_travel_max_lift", "filament_travel_slope",
        "filament_travel_lift_before_obstacle",
    }},
    {"are ignored, set retract_before_travel and retract_layer_change instead", {
        "filament_retract_before_travel", "filament_retract_layer_change",
    }},
    {"are ignored, there are no fan commands", {
        "fan_always_on", "fan_below_layer_time", "min_fan_speed", "max_fan_speed", "bridge_fan_speed", "disable_fan_first_layers",
        "full_fan_speed_layer", "enable_dynamic_fan_speeds",
    }},
    {"are ignored, the speeds are set by the speed options of this block and the travel speed by Aion", {
        "perimeter_speed", "external_perimeter_speed", "small_perimeter_speed", "infill_speed", "solid_infill_speed",
        "top_solid_infill_speed", "gap_fill_speed", "bridge_speed", "over_bridge_speed", "support_material_speed",
        "support_material_interface_speed", "first_layer_speed", "first_layer_infill_speed", "first_layer_speed_over_raft",
        "travel_speed", "travel_speed_z", "max_print_speed",
    }},
    {"are not supported and are ignored, the speeds are not limited", {
        "enable_dynamic_overhang_speeds", "max_volumetric_speed", "filament_max_volumetric_speed",
        "max_volumetric_extrusion_rate_slope_positive", "max_volumetric_extrusion_rate_slope_negative",
        "filament_infill_max_speed", "filament_infill_max_crossing_speed",
    }},
    {"are ignored, accelerations and machine limits come from Aion's machine configuration", {
        "default_acceleration", "perimeter_acceleration", "external_perimeter_acceleration", "infill_acceleration",
        "solid_infill_acceleration", "top_solid_infill_acceleration", "bridge_acceleration", "first_layer_acceleration",
        "first_layer_acceleration_over_raft", "travel_acceleration", "travel_short_distance_acceleration", "machine_limits_usage",
    }},
    {"are ignored, flow and temperatures are set in Aion's tool and material", {
        "extrusion_multiplier", "filament_diameter", "temperature", "first_layer_temperature", "bed_temperature",
        "first_layer_bed_temperature", "chamber_temperature", "chamber_minimal_temperature", "idle_temperature",
        "autoemit_temperature_commands", "use_volumetric_e", "use_relative_e_distances", "extrusion_axis",
    }},
    {"are ignored, the G-code is generated by Aion", {
        "start_gcode", "end_gcode", "before_layer_gcode", "layer_gcode", "toolchange_gcode", "between_objects_gcode",
        "color_change_gcode", "pause_print_gcode", "template_custom_gcode", "start_filament_gcode", "end_filament_gcode",
        "gcode_flavor", "gcode_comments", "gcode_label_objects", "gcode_substitutions", "binary_gcode", "output_filename_format",
        "post_process", "z_offset",
    }},
    {"are not supported and are ignored", {
        "arc_fitting", "only_retract_when_crossing_perimeters", "avoid_crossing_perimeters", "avoid_crossing_curled_overhangs",
        "cooling_slowdown_logic", "cooling_perimeter_transition_distance",
    }},
    {"are not supported, a normal seam is placed at the start of the scarf instead", {
        "scarf_seam_placement",
    }},
};

// PrusaSlicer options that prefill the "Configuration options" field, grouped by the comment line above them.
// Not listed: options from applyConfigOptionsToSlicerConfig, the speeds, UNSUPPORTED_OPTIONS and the options without effect for single tool machines
static const std::vector<std::pair<const char*, std::vector<const char*>>> SLICER_OPTION_GROUPS{
    {"Layers and perimeters", {
        "top_solid_min_thickness", "bottom_solid_min_thickness", "extra_perimeters", "extra_perimeters_on_overhangs",
        "ensure_vertical_shell_thickness", "thin_walls", "thick_bridges", "overhangs", "top_one_perimeter_type",
        "only_one_perimeter_first_layer", "external_perimeters_first", "gap_fill_enabled", "seam_position",
        "staggered_inner_seams", "seam_gap_distance", "prefer_clockwise_movements",
    }},
    {"Arachne perimeter generator", {
        "wall_transition_length", "wall_transition_filter_deviation", "wall_transition_angle", "wall_distribution_count",
        "min_feature_size", "min_bead_width",
    }},
    {"Fuzzy skin", {
        "fuzzy_skin", "fuzzy_skin_thickness", "fuzzy_skin_point_dist",
    }},
    {"Infill", {
        "top_fill_pattern", "bottom_fill_pattern", "fill_angle", "bridge_angle", "infill_anchor", "infill_anchor_max",
        "infill_overlap", "infill_every_layers", "automatic_infill_combination", "automatic_infill_combination_max_layer_height",
        "solid_infill_every_layers", "solid_infill_below_area", "infill_first",
    }},
    {"Ironing", {
        "ironing", "ironing_type", "ironing_flowrate", "ironing_spacing", "ironing_speed",
    }},
    {"Skirt and brim", {
        "skirts", "skirt_distance", "skirt_height", "draft_shield", "min_skirt_length", "brim_type", "brim_separation",
    }},
    {"Support material", {
        "support_material_style", "support_material_threshold", "support_material_enforce_layers", "dont_support_bridges",
        "support_material_contact_distance", "support_material_bottom_contact_distance", "support_material_pattern",
        "support_material_with_sheath", "support_material_spacing", "support_material_angle", "support_material_closing_radius",
        "support_material_xy_spacing", "support_material_interface_layers", "support_material_bottom_interface_layers",
        "support_material_interface_pattern", "support_material_interface_spacing", "support_material_interface_contact_loops",
        "support_material_synchronize_layers", "min_layer_height", "max_layer_height",
    }},
    {"Organic support", {
        "support_tree_angle", "support_tree_angle_slow", "support_tree_branch_diameter", "support_tree_branch_diameter_angle",
        "support_tree_branch_diameter_double_wall", "support_tree_tip_diameter", "support_tree_branch_distance",
        "support_tree_top_rate",
    }},
    {"Raft", {
        "raft_layers", "raft_contact_distance", "raft_expansion", "raft_first_layer_density", "raft_first_layer_expansion",
    }},
    {"Slicing", {
        "slice_closing_radius", "slicing_mode", "resolution", "gcode_resolution", "xy_size_compensation",
        "elefant_foot_compensation", "bridge_flow_ratio", "interface_shells", "filament_shrinkage_compensation_xy",
        "filament_shrinkage_compensation_z",
    }},
    {"Minimum layer time", {
        "cooling", "slowdown_below_layer_time", "min_print_speed",
    }},
    {"Retraction of the tool", {
        "retract_before_travel", "retract_layer_change",
    }},
};

// Directory of this plugin's .rhp file
static std::filesystem::path getPluginDirectory()
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&getPluginDirectory), &module);
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(module, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

// Load the PrusaSlicer defaults, overlaid with the baseline config shipped next to the plugin.
// @param s_config[out]: the loaded slicer config, only the PrusaSlicer defaults if the baseline config is missing or invalid
// @param errors[out]: an info if the baseline config is missing, a warning if it can't be read
static void _loadBaselineSlicerConfig(Slic3r::DynamicPrintConfig& s_config, Aion::Errorlist& errors)
{
    s_config = Slic3r::DynamicPrintConfig::full_print_config();
    const std::filesystem::path baseline_path = getPluginDirectory() / BASELINE_CONFIG_FILENAME;
    if (std::filesystem::exists(baseline_path)) {
        try {
            s_config.load_from_ini(baseline_path.u8string(), Slic3r::ForwardCompatibilitySubstitutionRule::EnableSilent);
        }
        catch (const std::exception& exc) {
            s_config = Slic3r::DynamicPrintConfig::full_print_config();
            errors.push_back(Aion::Error(Aion::ErrorClass::Warning, std::string("Could not read baseline slicer config, PrusaSlicer defaults are used: ") + exc.what()));
        }
    }
    else {
        errors.push_back(Aion::Error(Aion::ErrorClass::Info, "Baseline slicer config not found, PrusaSlicer defaults are used."));
    }
}

// Default text of the "Configuration options" field: the options of SLICER_OPTION_GROUPS with their values from PrusaSlicer defaults overlaid with the baseline config. Enums get their possible values as a comment.
static const std::string& getDefaultSlicerOptions()
{
    // created as a static variable to avoid repeatedly loading the baseline config and creating the text on every call
    static const std::string text = [] {
        Slic3r::DynamicPrintConfig s_config;
        Aion::Errorlist errors;
        _loadBaselineSlicerConfig(s_config, errors);

        std::string result;
        for (const auto& [group, keys] : SLICER_OPTION_GROUPS) {
            // blank line between the groups
            if (!result.empty()) {
                result += "\n\n";
            }
            result += std::string("# ") + group;
            for (const char* key : keys) {
                const Slic3r::ConfigOption* s_option = s_config.option(key);
                const Slic3r::ConfigOptionDef* s_def = Slic3r::print_config_def.get(key);
                // only if a key was removed in a newer PrusaSlicer version
                if (!s_option || !s_def) {
                    continue;
                }
                result += "\n" + std::string(key) + " = " + s_option->serialize();
                // only closed enums, the numerical options with an enum_def only have GUI presets as values
                if (s_def->type == Slic3r::coEnum && s_def->enum_def) {
                    std::string values;
                    for (const std::string& value : s_def->enum_def->values()) {
                        values += (values.empty() ? "" : ", ") + value;
                    }
                    result += " # " + values;
                }
            }
        }
        return result;
    }();
    return text;
}

CamSlicer::CamSlicer(const ON_UUID& plugin_uuid)
    : CamBase("Unnamed CamSlicer", ON_UuidFromString(L"6DB9405E-BC94-498E-877C-E427D8288C5B"), plugin_uuid)
{
    // init options of this cam block
    m_volume_components = new Aion::ConfigOptionComponentVector();
    this->m_options.addOption("volumeUUIDs", m_volume_components);
    m_volume_components->label = "Volumes";
    m_volume_components->tooltip = "Structures to be printed";

    m_orientation_vector = new Aion::ConfigOptionDirection(ON_3dVector::ZAxis);
    this->m_options.addOption("orientationVector", m_orientation_vector);
    m_orientation_vector->label = "Orientation vector";
    m_orientation_vector->tooltip = "Slicing direction (build direction) of the volumes";
    m_orientation_vector->unit = "";
    m_orientation_vector->digits = 4;

    // Get the machine orientation vector from the machine config, if available. Otherwise, use the default Z axis.
    const Aion::DrawingConfig* drawing_config = getConfig();
    if (drawing_config) {
        ON_Xform workpiece_plane = drawing_config->machineConfig().getWorkpiecePlaneTransform();
        if (workpiece_plane.IsValid()) {
            ON_3dVector orientation_vector = workpiece_plane * ON_3dVector::ZAxis;
            m_orientation_vector->setPoint(orientation_vector.x, orientation_vector.y, orientation_vector.z);
        }
    }

    m_tool = new Aion::ConfigOptionTool();
    this->m_options.addOption("tool", m_tool);
    m_tool->label = "Tool";
    m_tool->tooltip = "Select a tool for the CAM-block";
    m_tool->tooltype_filter.push_back(Aion::ToolType::ttVolumetric);
    m_tool->tooltype_filter.push_back(Aion::ToolType::ttExtruder);

    m_last_tool = new Aion::ConfigOptionTool();
    this->m_options.addOption("lastTool", m_last_tool);
    m_last_tool->detail_level = Aion::ConfigDetailLevel::levelInvisible;

    // Extrusion parameters
    m_width = new Aion::ConfigOptionFloat(0.4);
    this->m_options.addOption("width", m_width);
    m_width->label = "Width";
    m_width->tooltip = "Extrusion width";
    m_width->unit = "mm";
    m_width->min = 0.0;
    m_width->increment = 0.1;

    m_height = new Aion::ConfigOptionFloat(0.2);
    this->m_options.addOption("height", m_height);
    m_height->label = "Height";
    m_height->tooltip = "Layer height";
    m_height->unit = "mm";
    m_height->min = 0.0;
    m_height->increment = 0.1;

    m_first_layer_height = new Aion::ConfigOptionFloat(0.2);
    this->m_options.addOption("firstLayerHeight", m_first_layer_height);
    m_first_layer_height->label = "First layer height";
    m_first_layer_height->tooltip = "Height of the first layer.";
    m_first_layer_height->unit = "mm";
    m_first_layer_height->min = 0.0;
    m_first_layer_height->increment = 0.1;
    m_first_layer_height->detail_level = Aion::ConfigDetailLevel::levelTwo;

    m_speed = new Aion::ConfigOptionFloat(20.0);
    this->m_options.addOption("speed", m_speed);
    m_speed->label = "Print speed";
    m_speed->tooltip = "Base print speed. The speeds of the individual extrusion types are percentages of this value.";
    m_speed->unit = "mm/s";
    m_speed->min = 0.0;
    m_speed->increment = 5.0;

    m_speed_factors = new Aion::ConfigOptionContainer("<Edit>", "Edit speed factors", ON_Color(0, 0, 0));
    this->m_options.addOption("speedFactors", m_speed_factors);
    m_speed_factors->label = "Speed factors";
    m_speed_factors->tooltip = "Speeds of the individual extrusion types in % of the print speed";
    m_speed_factors->detail_level = Aion::ConfigDetailLevel::levelTwo;
    // Only append to the container, its deserialization depends on the order of the options.
    m_perimeter_speed = new Aion::ConfigOptionPercent(100.0);
    m_speed_factors->addOption("perimeters", m_perimeter_speed);
    m_perimeter_speed->label = "Perimeters";
    m_perimeter_speed->tooltip = "Perimeter speed in % of the print speed";
    m_perimeter_speed->max = 1000.0;
    m_perimeter_speed->min = 0.1;
    m_perimeter_speed->increment = 10.0;

    m_small_perimeter_speed = new Aion::ConfigOptionPercent(80.0);
    m_speed_factors->addOption("smallPerimeters", m_small_perimeter_speed);
    m_small_perimeter_speed->label = "Small perimeters";
    m_small_perimeter_speed->tooltip = "Small perimeter speed in % of the print speed";
    m_small_perimeter_speed->max = 1000.0;
    m_small_perimeter_speed->min = 0.1;
    m_small_perimeter_speed->increment = 10.0;

    m_external_perimeter_speed = new Aion::ConfigOptionPercent(80.0);
    m_speed_factors->addOption("externalPerimeters", m_external_perimeter_speed);
    m_external_perimeter_speed->label = "External perimeters";
    m_external_perimeter_speed->tooltip = "External perimeter speed in % of the print speed";
    m_external_perimeter_speed->max = 1000.0;
    m_external_perimeter_speed->min = 0.1;
    m_external_perimeter_speed->increment = 10.0;

    m_infill_speed = new Aion::ConfigOptionPercent(150.0);
    m_speed_factors->addOption("infill", m_infill_speed);
    m_infill_speed->label = "Infill";
    m_infill_speed->tooltip = "Infill speed in % of the print speed";
    m_infill_speed->max = 1000.0;
    m_infill_speed->min = 0.1;
    m_infill_speed->increment = 10.0;

    m_solid_infill_speed = new Aion::ConfigOptionPercent(150.0);
    m_speed_factors->addOption("solidInfill", m_solid_infill_speed);
    m_solid_infill_speed->label = "Solid infill";
    m_solid_infill_speed->tooltip = "Solid infill speed in % of the print speed";
    m_solid_infill_speed->max = 1000.0;
    m_solid_infill_speed->min = 0.1;
    m_solid_infill_speed->increment = 10.0;

    m_top_solid_infill_speed = new Aion::ConfigOptionPercent(100.0);
    m_speed_factors->addOption("topSolidInfill", m_top_solid_infill_speed);
    m_top_solid_infill_speed->label = "Top solid infill";
    m_top_solid_infill_speed->tooltip = "Top solid infill speed in % of the print speed";
    m_top_solid_infill_speed->max = 1000.0;
    m_top_solid_infill_speed->min = 0.1;
    m_top_solid_infill_speed->increment = 10.0;

    m_support_speed = new Aion::ConfigOptionPercent(100.0);
    m_speed_factors->addOption("support", m_support_speed);
    m_support_speed->label = "Support";
    m_support_speed->tooltip = "Support speed in % of the print speed";
    m_support_speed->max = 1000.0;
    m_support_speed->min = 0.1;
    m_support_speed->increment = 10.0;

    m_support_interface_speed = new Aion::ConfigOptionPercent(100.0);
    m_speed_factors->addOption("supportInterface", m_support_interface_speed);
    m_support_interface_speed->label = "Support interface";
    m_support_interface_speed->tooltip = "Support interface speed in % of the print speed";
    m_support_interface_speed->max = 1000.0;
    m_support_interface_speed->min = 0.1;
    m_support_interface_speed->increment = 10.0;

    m_bridge_speed = new Aion::ConfigOptionPercent(50.0);
    m_speed_factors->addOption("bridges", m_bridge_speed);
    m_bridge_speed->label = "Bridges";
    m_bridge_speed->tooltip = "Bridge speed in % of the print speed";
    m_bridge_speed->max = 1000.0;
    m_bridge_speed->min = 0.1;
    m_bridge_speed->increment = 10.0;

    m_over_bridge_speed = new Aion::ConfigOptionPercent(80.0);
    m_speed_factors->addOption("overBridges", m_over_bridge_speed);
    m_over_bridge_speed->label = "Over bridges";
    m_over_bridge_speed->tooltip = "Speed of the layers above bridges in % of the print speed";
    m_over_bridge_speed->max = 1000.0;
    m_over_bridge_speed->min = 0.1;
    m_over_bridge_speed->increment = 10.0;

    m_gap_fill_speed = new Aion::ConfigOptionPercent(50.0);
    m_speed_factors->addOption("gapFill", m_gap_fill_speed);
    m_gap_fill_speed->label = "Gap fill";
    m_gap_fill_speed->tooltip = "Gap fill speed in % of the print speed";
    m_gap_fill_speed->max = 1000.0;
    m_gap_fill_speed->min = 0.1;
    m_gap_fill_speed->increment = 10.0;

    m_first_layer_speed = new Aion::ConfigOptionPercent(50.0);
    m_speed_factors->addOption("firstLayer", m_first_layer_speed);
    m_first_layer_speed->label = "First layer";
    m_first_layer_speed->tooltip = "Speed of all extrusions on the first layer in % of the print speed";
    m_first_layer_speed->max = 1000.0;
    m_first_layer_speed->min = 0.1;
    m_first_layer_speed->increment = 10.0;

    // Filling options
    m_perimeters = new Aion::ConfigOptionInt(3);
    this->m_options.addOption("perimeters", m_perimeters);
    m_perimeters->label = "Perimeter loops";
    m_perimeters->tooltip = "Number of perimeter loops";
    m_perimeters->min = 0;

    m_perimeter_generator = new Aion::ConfigOptionEnum(PerimeterGenerator::arachne, &GetPerimeterGeneratorStringMap());
    this->m_options.addOption("perimeterGenerator", m_perimeter_generator);
    m_perimeter_generator->label = "Perimeter generator";
    m_perimeter_generator->tooltip = "Arachne: variable extrusion width to fill thin walls and gaps\nClassic: constant extrusion width";
    m_perimeter_generator->detail_level = Aion::ConfigDetailLevel::levelTwo;

    m_vase_mode = new Aion::ConfigOptionBool(false);
    this->m_options.addOption("vaseMode", m_vase_mode);
    m_vase_mode->label = "Vase mode";
    m_vase_mode->tooltip = "Print only a single perimeter without top layers, infill and supports";
    m_vase_mode->detail_level = Aion::ConfigDetailLevel::levelTwo;

    m_top_layers = new Aion::ConfigOptionInt(5);
    this->m_options.addOption("topLayers", m_top_layers);
    m_top_layers->label = "Top layers";
    m_top_layers->tooltip = "Number of solid layers at the top";
    m_top_layers->min = 0;
    m_top_layers->detail_level = Aion::ConfigDetailLevel::levelTwo;

    m_bottom_layers = new Aion::ConfigOptionInt(4);
    this->m_options.addOption("bottomLayers", m_bottom_layers);
    m_bottom_layers->label = "Bottom layers";
    m_bottom_layers->tooltip = "Number of solid layers at the bottom";
    m_bottom_layers->min = 0;
    m_bottom_layers->detail_level = Aion::ConfigDetailLevel::levelTwo;

    m_infill_density = new Aion::ConfigOptionPercent(15.0);
    this->m_options.addOption("infillDensity", m_infill_density);
    m_infill_density->label = "Infill";
    m_infill_density->tooltip = "Density of the sparse infill";
    m_infill_density->increment = 5.0;

    m_infill_pattern = new Aion::ConfigOptionEnum(DEFAULT_INFILL_PATTERN, &GetInfillPatternStringMap());
    this->m_options.addOption("infillPattern", m_infill_pattern);
    m_infill_pattern->label = "Infill pattern";
    m_infill_pattern->tooltip = "Pattern of the sparse infill";
    m_infill_pattern->detail_level = Aion::ConfigDetailLevel::levelTwo;

    // Other
    m_support = new Aion::ConfigOptionEnum(SupportMode::supportNone, &GetSupportModeStringMap());
    this->m_options.addOption("supportMode", m_support);
    m_support->label = "Support";
    m_support->tooltip = "Generate supports for this CAM-Block.\nNone: no supports\nBottom only: only supports that start on the printbed\nEverywhere: supports may also start on the structure itself\nOnly use for structures on the printbed.";

    m_brim_width = new Aion::ConfigOptionFloat(0.0);
    this->m_options.addOption("brimWidth", m_brim_width);
    m_brim_width->label = "Brim";
    m_brim_width->tooltip = "Width of the brim around the first layer. 0 disables the brim.\nOnly use for structures on the printbed.";
    m_brim_width->unit = "mm";
    m_brim_width->min = 0.0;
    m_brim_width->detail_level = Aion::ConfigDetailLevel::levelTwo;

    m_travel_z_hop = new Aion::ConfigOptionFloat(0.5);
    this->m_options.addOption("travelZHop", m_travel_z_hop);
    m_travel_z_hop->label = "Travel Z hop";
    m_travel_z_hop->tooltip = "Height the tool is lifted along the tool direction for the travel moves between the slicer paths.\n0 travels without lifting the tool. -1 disables these travel moves, Aion then inserts its global travel moves between all paths.";
    m_travel_z_hop->unit = "mm";
    m_travel_z_hop->min = -1.0;
    m_travel_z_hop->detail_level = Aion::ConfigDetailLevel::levelTwo;

    m_slicer_options = new Aion::ConfigOptionString(getDefaultSlicerOptions());
    this->m_options.addOption("slicerOptions", m_slicer_options);
    m_slicer_options->label = "Configuration options";
    m_slicer_options->tooltip = "PrusaSlicer options in \".ini\" format (one \"key = value\" per line), \"#\" starts a comment.\nThey are applied last and overwrite all other settings.\nThe comment after a text value lists its possible values.";
    m_slicer_options->multiline = true;
    m_slicer_options->detail_level = Aion::ConfigDetailLevel::levelTwo;
}

Aion::CamBase* CamSlicer::createNewInstance() const
{
    CamSlicer* result = new CamSlicer(this->getPluginUuid());
    return result;
}

void CamSlicer::applyConfigOptionsToSlicerConfig(Slic3r::DynamicPrintConfig& s_config)
{
    const double width = m_width->getFloat();
    for (const char* key : EXTRUSION_WIDTH_KEYS) {
        s_config.set_deserialize_strict({ { key, width } });
    }
    
    // Aion has no nozzle, PrusaSlicer uses the nozzle diameter as bead size (e.g. for Arachne and overhang thresholds) we set the nozzle diameter to the extrusion width
    s_config.set_deserialize_strict({
        { "layer_height", m_height->getFloat() },
        { "first_layer_height", m_first_layer_height->getFloat() },
        { "nozzle_diameter", width },
    });

    s_config.set_deserialize_strict({
        { "perimeters", m_perimeters->getInt() },
        { "perimeter_generator", m_perimeter_generator->getInt() == PerimeterGenerator::classic ? "classic" : "arachne" },
        { "top_solid_layers", m_top_layers->getInt() },
        { "bottom_solid_layers", m_bottom_layers->getInt() },
        { "fill_density", m_infill_density->serialize() },
        { "fill_pattern", INFILL_PATTERNS.at(m_infill_pattern->getInt()).second },
        { "support_material", m_support->getInt() != SupportMode::supportNone },
        { "support_material_auto", true },
        { "support_material_buildplate_only", m_support->getInt() == SupportMode::supportBottomOnly },
        { "brim_width", m_brim_width->getFloat() },
        { "spiral_vase", m_vase_mode->getBool() },
    });

    if (m_vase_mode->getBool()) {
        // same overrides as PrusaSlicer applies for spiral vase mode
        s_config.set_deserialize_strict({
            { "perimeters", 1 },
            { "top_solid_layers", 0 },
            { "fill_density", "0%" },
            { "support_material", false },
        });
    }
}

bool CamSlicer::loadSlicerConfig(Slic3r::DynamicPrintConfig& s_config, Aion::Errorlist& errors)
{
    // PrusaSlicer defaults, overlaid with the shipped baseline config
    _loadBaselineSlicerConfig(s_config, errors);

    applyConfigOptionsToSlicerConfig(s_config);

    // user defined options overwrite everything else
    if (!m_slicer_options->getString().empty()) {
        try {
            // read the ini file
            std::istringstream lines(m_slicer_options->getString());
            std::string ini;
            for (std::string line; std::getline(lines, line);) {
                ini += line.substr(0, line.find('#')) + "\n";
            }
            boost::property_tree::ptree tree;
            std::istringstream iss(ini);
            boost::property_tree::read_ini(iss, tree);
            // load() silently ignores keys that handle_legacy clears (unknown or obsolete keys)
            std::string unknown_keys;
            for (const boost::property_tree::ptree::value_type& v : tree) {
                std::string key = v.first;
                std::string value = v.second.data();
                Slic3r::PrintConfigDef::handle_legacy(key, value);
                if (key.empty()) {
                    // append the unkown keys
                    unknown_keys += (unknown_keys.empty() ? "" : ", ") + v.first;
                }
            }
            if (!unknown_keys.empty()) {
                errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Unknown or obsolete slicer options in configuration options: " + unknown_keys));
                return false;
            }
            // Disable throws on invalid values instead of silently substituting them
            s_config.load(tree, Slic3r::ForwardCompatibilitySubstitutionRule::Disable);
        }
        catch (const std::exception& exc) {
            errors.push_back(Aion::Error(Aion::ErrorClass::Warning, std::string("Could not parse configuration options: ") + exc.what()));
            return false;
        }
    }
    return true;
}

// Generate the Slic3r TriangleMesh from the Rhino ON_Mesh
// Quads are split into two triangles and inside out meshes are flipped.
// @param on_mesh: the Rhino ON_Mesh to convert
// @param xform: transformation from scene to slicer coordinates
// @param s_mesh[out]: the created Slic3r TriangleMesh
// @param errors[out]: a warning is added if the mesh can not be converted, an info if it has open or non-manifold edges
// @return false if the mesh could not be converted
bool _createSlicerMesh(const ON_Mesh& on_mesh, const ON_Xform& xform, Slic3r::TriangleMesh& s_mesh, Aion::Errorlist& errors) {
    const int vertex_count = on_mesh.VertexCount();
    // the double precision vertices are more accurate, but only usable if they are in sync with the float vertices
    const bool use_double_vertices = on_mesh.HasSynchronizedDoubleAndSinglePrecisionVertices();

    indexed_triangle_set s_its;
    s_its.vertices.reserve(vertex_count);
    for (int i = 0; i < vertex_count; i++) {
        ON_3dPoint on_vert = use_double_vertices ? on_mesh.m_dV[i] : ON_3dPoint(on_mesh.m_V[i]);
        on_vert.Transform(xform);
        if (!on_vert.IsValid()) {
            errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Slicer object has invalid mesh vertices."));
            return false;
        }
        s_its.vertices.emplace_back(float(on_vert.x), float(on_vert.y), float(on_vert.z));
    }

    s_its.indices.reserve(on_mesh.FaceCount() + on_mesh.QuadCount());
    for (int i = 0; i < on_mesh.FaceCount(); i++) {
        const ON_MeshFace& on_face = on_mesh.m_F[i];
        // out of range indices would crash in PrusaSlicer without an exception
        if (!on_face.IsValid(vertex_count)) {
            errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Slicer object has invalid mesh faces."));
            return false;
        }
        s_its.indices.emplace_back(on_face.vi[0], on_face.vi[1], on_face.vi[2]);
        if (on_face.IsQuad()) {
            s_its.indices.emplace_back(on_face.vi[0], on_face.vi[2], on_face.vi[3]);
        }
    }

    s_mesh = Slic3r::TriangleMesh(std::move(s_its));
    if (s_mesh.empty()) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Slicer object could not be meshed, the mesh is empty."));
        return false;
    }
    // PrusaSlicer expects outward facing normals, inside out meshes have a negative volume
    if (s_mesh.stats().volume < 0) {
        s_mesh.flip_triangles();
    }
    // open meshes are already reported by createNurbsMesh, only report the non-manifold edges of closed meshes
    if (!s_mesh.stats().manifold() && on_mesh.IsClosed()) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Info, "Slicer object has " + std::to_string(s_mesh.stats().open_edges) + " non-manifold edges. Slicing results may be incomplete."));
    }
    return true;
}

double CamSlicer::getSpeedForExtrusionRole(Slic3r::ExtrusionRole s_role, bool is_small_perimeter, bool is_first_layer) const {
    // all speed factors are percentages of the print speed
    const double speed = m_speed->getFloat();
    const double factor = speed / 100.0;
    // the first layer speed overrides all other speeds
    if (is_first_layer) {
        return factor * m_first_layer_speed->getFloat();
    }
    if (is_small_perimeter) {
        return factor * m_small_perimeter_speed->getFloat();
    }
    if (s_role == Slic3r::ExtrusionRole::Perimeter) {
        return factor * m_perimeter_speed->getFloat();
    }
    if (s_role == Slic3r::ExtrusionRole::ExternalPerimeter) {
        return factor * m_external_perimeter_speed->getFloat();
    }
    // bridge infill and overhang perimeters
    if (s_role.is_bridge()) {
        return factor * m_bridge_speed->getFloat();
    }
    if (s_role == Slic3r::ExtrusionRole::InternalInfill) {
        return factor * m_infill_speed->getFloat();
    }
    if (s_role == Slic3r::ExtrusionRole::SolidInfill) {
        return factor * m_solid_infill_speed->getFloat();
    }
    if (s_role == Slic3r::ExtrusionRole::InfillOverBridge) {
        return factor * m_over_bridge_speed->getFloat();
    }
    if (s_role == Slic3r::ExtrusionRole::TopSolidInfill) {
        return factor * m_top_solid_infill_speed->getFloat();
    }
    if (s_role == Slic3r::ExtrusionRole::GapFill) {
        return factor * m_gap_fill_speed->getFloat();
    }
    // skirt and brim are printed with the support speed like in PrusaSlicer
    if (s_role == Slic3r::ExtrusionRole::SupportMaterial || s_role == Slic3r::ExtrusionRole::Skirt) {
        return factor * m_support_speed->getFloat();
    }
    if (s_role == Slic3r::ExtrusionRole::SupportMaterialInterface) {
        return factor * m_support_interface_speed->getFloat();
    }
    // ironing speed it taken from the slicer config.
    if (s_role == Slic3r::ExtrusionRole::Ironing) {
        return m_print_config.ironing_speed.value;
    }
    // roles without an own speed use the print speed
    return speed;
}

void CamSlicer::createToolpathForSlicerpath(Aion::ToolpathPath* const path_ptr, const Slic3r::GCode::SmoothPathElement& s_element, double path_z, const ON_Xform& transform, const ON_3dVector& dir, bool is_small_perimeter, bool is_first_layer) const {
    const Slic3r::ExtrusionAttributes& s_attributes = s_element.path_attributes;
    // the speed is constant along the path
    const double speed = getSpeedForExtrusionRole(s_attributes.role, is_small_perimeter, is_first_layer);

    // the smooth path cache is empty, so all segments are linear and only their end points are needed
    for (const Slic3r::Geometry::ArcWelder::Segment& s_segment : s_element.path) {
        const Slic3r::Point& s_point = s_segment.point;
        ON_3dPoint on_point(Slic3r::unscale<double>(s_point.x()), Slic3r::unscale<double>(s_point.y()), path_z);
        on_point.Transform(transform);
        // only add new points if they are not equal to the last point,
        Aion::ToolpathElement* last_point = path_ptr->getLastPoint();
        if (!last_point || last_point->getPoint().DistanceTo(on_point) > slicerEPS) {
            path_ptr->appendPoint(on_point, dir);
            last_point = path_ptr->getLastPoint();
        }
        // setWidth must be called first, it sets the height that setmm3_per_mm needs. setmm3_per_mm then
        // recomputes the width as mm3_per_mm / height, so the extruded volume matches the slicer (rounded extrusion edges).
        last_point->setWidth(s_attributes.width, s_attributes.height);
        last_point->setmm3_per_mm(s_attributes.mm3_per_mm);
        last_point->setSpeed(speed);
    }
}

// Converts an extrusion entity into a SmoothPath for Slic3r::GCode::ExtrusionOrder::get_extrusions
// Stripped down version of Slic3r::GCode::SmoothPathGenerator which is private and not exported. 
struct SlicerPathSmoothing {
    // seam placer for the perimeter loops, nullptr to start them at the point closest to the previous position (vase mode)
    const Slic3r::Seams::Placer* seam_placer;
    // scaled gcode_resolution
    double resolution;
    // scaled seam_gap_distance, positive to shorten the loops before the seam, negative to extend them after the seam
    double seam_gap;
    Slic3r::GCode::SmoothPathCache smooth_path_cache;

    // Convert the extrusion entity into a SmoothPath
    Slic3r::GCode::ExtrusionOrder::PathSmoothingResult operator()(const Slic3r::Layer* s_layer, const Slic3r::PrintRegion* s_region, const Slic3r::ExtrusionEntityReference& s_ref, const unsigned extruder_id, std::optional<Slic3r::GCode::ExtrusionOrder::InstancePoint>& previous_position) const {
        const Slic3r::ExtrusionEntity* s_entity = &s_ref.extrusion_entity();
        Slic3r::GCode::SmoothPath s_path;

        // loop seams are placed according to the seam placement strategy
        if (const Slic3r::ExtrusionLoop* s_loop = dynamic_cast<const Slic3r::ExtrusionLoop*>(s_entity)) {
            if (s_loop->paths.empty() || s_loop->paths.front().empty()) {
                return { s_path, 0 };
            }
            // loops start at the point closest to the previous position
            Slic3r::Point seam = Slic3r::Point::Zero();
            if (previous_position) {
                seam = previous_position->local_point;
            }
            // when a seam placer is available, the seam is placed according to the seam placement strategy (handled in libslic3r code)
            if (seam_placer && s_loop->role().is_perimeter() && s_layer && s_region) {
                const boost::variant<Slic3r::Point, Slic3r::Seams::Scarf::Scarf> s_seam = seam_placer->place_seam(s_layer, s_region, *s_loop, s_ref.flipped(), seam);
                // scarf seams are not supported, the loop starts at the start point of the scarf
                if (const Slic3r::Point* s_seam_point = boost::get<Slic3r::Point>(&s_seam)) {
                    seam = *s_seam_point;
                }
                else {
                    seam = boost::get<Slic3r::Seams::Scarf::Scarf>(s_seam).start_point;
                }
            }
            // seam points closer than 1.5 microns to an existing point are merged
            s_path = smooth_path_cache.resolve_or_fit_split_with_seam(*s_loop, s_ref.flipped(), resolution, seam, Slic3r::scaled<double>(0.0015));

            // when a seam gap is configured, the loop is shortened or extended at the end
            if (seam_gap > 0) {
                Slic3r::GCode::clip_end(s_path, seam_gap, Slic3r::scaled<double>(Slic3r::GCode::ExtrusionOrder::min_gcode_segment_length));
            }
            else if (seam_gap < 0) {
                // append the start of the loop again, so that the loop overlaps itself
                Slic3r::GCode::SmoothPath s_extension = s_path;
                Slic3r::GCode::clip_end(s_extension, Slic3r::GCode::length(s_path) + seam_gap, Slic3r::scaled<double>(Slic3r::GCode::ExtrusionOrder::min_gcode_segment_length));
                Slic3r::append(s_path, s_extension);
            }
        }
        // multipaths and paths are smoothed
        else if (const Slic3r::ExtrusionMultiPath* s_multipath = dynamic_cast<const Slic3r::ExtrusionMultiPath*>(s_entity)) {
            s_path = smooth_path_cache.resolve_or_fit(*s_multipath, s_ref.flipped(), resolution);
        }
        else if (const Slic3r::ExtrusionPath* s_single_path = dynamic_cast<const Slic3r::ExtrusionPath*>(s_entity)) {
            s_path = Slic3r::GCode::SmoothPath{ Slic3r::GCode::SmoothPathElement{ s_single_path->attributes(), smooth_path_cache.resolve_or_fit(*s_single_path, s_ref.flipped(), resolution) } };
        }

        // the next extrusion is sorted starting at the end of this one
        for (auto it = s_path.rbegin(); it != s_path.rend(); ++it) {
            if (!it->path.empty()) {
                previous_position = Slic3r::GCode::ExtrusionOrder::InstancePoint{ it->path.back().point };
                break;
            }
        }
        return { s_path, 0 };
    }
};

void CamSlicer::processSmoothPath(const Slic3r::GCode::SmoothPath& s_path, double path_z, bool is_small_perimeter, bool is_first_layer, const Aion::ToolConfig* tool_config, const ON_Xform& transform, const ON_3dVector& dir, bool& layer_change, std::optional<Slic3r::Point>& last_pos)
{
    auto path = std::make_unique<Aion::ToolpathPath>(tool_config);
    Aion::ToolpathPath* const path_ptr = path.get();
    path_ptr->speed = m_speed->getFloat();
    // disable smart tool orientation to be safe.
    path_ptr->smart_tool_orientation = false;

    // store the last position of the previous path and the start position of this path.
    const std::optional<Slic3r::Point> previous_pos = last_pos;
    std::optional<Slic3r::Point> start_pos;
    size_t point_count = 0;
    for (const Slic3r::GCode::SmoothPathElement& s_element : s_path) {
        point_count += s_element.path.size();
        if (!start_pos && !s_element.path.empty()) {
            start_pos = s_element.path.front().point;
        }
    }
    path_ptr->getToolpathPoints()->reserve(point_count);
    for (const Slic3r::GCode::SmoothPathElement& s_element : s_path) {
        createToolpathForSlicerpath(path_ptr, s_element, path_z, transform, dir, is_small_perimeter, is_first_layer);
        if (!s_element.path.empty()) {
            last_pos = s_element.path.back().point;
        }
    }
    // skip empty and zero length extrusions
    if (path_ptr->getToolpathPoints()->size() < 2) {
        return;
    }

    // Get the last path to check if the travel distance is shorter than retract_before_travel.
    Aion::ToolpathPath* const last_path = m_toolpaths.empty() ? nullptr : dynamic_cast<Aion::ToolpathPath*>(m_toolpaths.back().get());
    const bool short_travel = last_path && previous_pos && start_pos && (!layer_change || !m_print_config.retract_layer_change.get_at(0)) &&
        Slic3r::unscale<double>((*start_pos - *previous_pos).cast<double>().norm()) < m_print_config.retract_before_travel.get_at(0);
    layer_change = false;

    // Generate wiping moves for the last_path with the retraction depending on short_travel.
    if (last_path) {
        last_path->setOverwriteRetraction(short_travel ? 0.0 : -1.0);
        this->generateWiping(last_path, false);
    }
    // Set the retraction for this path depending on short_travel, and set it back to -1.0 after generating the wiping moves.
    // Otherwise the wiping moves might be generated even if we have a short_travel
    path_ptr->setOverwriteRetraction(short_travel ? 0.0 : -1.0);
    this->generateWiping(path_ptr, true);
    path_ptr->setOverwriteRetraction(-1.0);

    // generate travel moves (short travels are always generated without z_hop)
    const double z_hop = short_travel ? 0.0 : m_travel_z_hop->getFloat();
    if (z_hop >= 0 &&!m_toolpaths.empty() && m_toolpaths.back()->hasToolpathPoints() && path_ptr->hasToolpathPoints()) {
        Aion::ToolpathElement& last = m_toolpaths.back()->getToolpathPoints()->back();
        if (last.getPoint().DistanceTo(path_ptr->getFirstPoint()->getPoint()) > slicerEPS) {
            auto travel = std::make_unique<Aion::ToolpathTravel>(tool_config);
            travel->smart_tool_orientation = false;
            ON_3dPoint from_p = last.getPoint();
            ON_3dVector from_v = last.getNormalIn();
            // start point
            travel->appendPoint(from_p, from_v);
            ON_3dPoint to_p = path_ptr->getFirstPoint()->getPoint();
            ON_3dVector to_v = path_ptr->getFirstPoint()->getNormalIn();
            if (z_hop > 0) {
                // lift
                from_v.Unitize();
                from_p.Transform(ON_Xform::TranslationTransformation(from_v * z_hop));
                travel->appendPoint(from_p, last.getNormalIn());
                // unlift
                to_v.Unitize();
                to_p.Transform(ON_Xform::TranslationTransformation(to_v * z_hop));
                travel->appendPoint(to_p, path_ptr->getFirstPoint()->getNormalIn());
            }
            // target point
            travel->appendPoint(path_ptr->getFirstPoint()->getPoint(), path_ptr->getFirstPoint()->getNormalIn());

            travel->height = last.getHeight();
            travel->getLastPoint()->setWidth(path_ptr->getFirstPoint()->getWidth(), path_ptr->getFirstPoint()->getHeight());

            travel->speed = getConfig()->machineConfig().travel_feedrate();
            m_toolpaths.emplace_back(std::move(travel));
        }
    }
    m_toolpaths.emplace_back(std::move(path));
}

void CamSlicer::processSortedExtrusions(const std::vector<Slic3r::GCode::ExtrusionOrder::ExtruderExtrusions>& s_extrusions, const Slic3r::GCode::ObjectLayerToPrint& s_layer_to_print, const Aion::ToolConfig* tool_config, const ON_Xform& transform, const ON_3dVector& dir, std::optional<Slic3r::Point>& last_pos)
{
    // get the layer and the support bottom_z (they are grouped by top_z)
    const double layer_z = s_layer_to_print.layer()->bottom_z();
    const double support_z = s_layer_to_print.support_layer ? s_layer_to_print.support_layer->bottom_z() : layer_z;
    // the travel to the first path of this layer is a layer change
    bool layer_change = true;
    // first toolpath of this layer, for the slowdown to the minimum layer time
    const size_t layer_begin = m_toolpaths.size();

    // this should always be one loop because there is only one tool.
    for (const Slic3r::GCode::ExtrusionOrder::ExtruderExtrusions& s_extruder : s_extrusions) {
        // Generate toolpath for skirt and brim
        for (const std::pair<std::size_t, Slic3r::GCode::SmoothPath>& s_skirt : s_extruder.skirt) {
            processSmoothPath(s_skirt.second, layer_z, false, layer_z < slicerEPS, tool_config, transform, dir, layer_change, last_pos);
        }
        for (const Slic3r::GCode::ExtrusionOrder::BrimPath& s_brim : s_extruder.brim) {
            processSmoothPath(s_brim.path, layer_z, false, layer_z < slicerEPS, tool_config, transform, dir, layer_change, last_pos);
        }

        for (const Slic3r::GCode::ExtrusionOrder::NormalExtrusions& s_normal : s_extruder.normal_extrusions) {
            // generate the support paths before printing the actual layers
            for (const Slic3r::GCode::ExtrusionOrder::SupportPath& s_support : s_normal.support_extrusions) {
                processSmoothPath(s_support.path, support_z, false, support_z < slicerEPS, tool_config, transform, dir, layer_change, last_pos);
            }
            // generate the actual layer paths
            for (const Slic3r::GCode::ExtrusionOrder::SliceExtrusions& s_slice : s_normal.slices_extrusions) {
                // print each island separately
                for (const Slic3r::GCode::ExtrusionOrder::IslandExtrusions& s_island : s_slice.common_extrusions) {
                    // when infill is first print infill then perimenter
                    if (s_island.infill_first) {
                        for (const Slic3r::GCode::ExtrusionOrder::InfillRange& s_range : s_island.infill_ranges) {
                            // print all infill paths
                            for (const Slic3r::GCode::SmoothPath& s_path : s_range.items) {
                                processSmoothPath(s_path, layer_z, false, layer_z < slicerEPS, tool_config, transform, dir, layer_change, last_pos);
                            }
                        }
                    }
                    // print all perimeter paths
                    for (const Slic3r::GCode::ExtrusionOrder::Perimeter& s_perimeter : s_island.perimeters) {
                        const bool is_small_perimeter = s_perimeter.extrusion_entity->role().is_perimeter() && s_perimeter.extrusion_entity->length() <= SMALL_PERIMETER_LENGTH;
                        processSmoothPath(s_perimeter.smooth_path, layer_z, is_small_perimeter, layer_z < slicerEPS, tool_config, transform, dir, layer_change, last_pos);
                    }
                    // otherwise print perimeter first then infill
                    if (!s_island.infill_first) {
                        // print all infill paths
                        for (const Slic3r::GCode::ExtrusionOrder::InfillRange& s_range : s_island.infill_ranges) {
                            for (const Slic3r::GCode::SmoothPath& s_path : s_range.items) {
                                processSmoothPath(s_path, layer_z, false, layer_z < slicerEPS, tool_config, transform, dir, layer_change, last_pos);
                            }
                        }
                    }
                }
                // ironing after all islands of the slice
                for (const Slic3r::GCode::ExtrusionOrder::InfillRange& s_range : s_slice.ironing_extrusions) {
                    for (const Slic3r::GCode::SmoothPath& s_path : s_range.items) {
                        // calculate the ironing z position.
                        const double ironing_z = s_path.empty() ? layer_z : s_layer_to_print.layer()->print_z - s_path.front().path_attributes.height;
                        processSmoothPath(s_path, ironing_z, false, layer_z < slicerEPS, tool_config, transform, dir, layer_change, last_pos);
                    }
                }
            }
        }
    }

    // check if cooling is enabled and slowdown_below_layer_time is set, otherwise skip the slowdown
    const double min_layer_time = m_print_config.slowdown_below_layer_time.get_at(0);
    if (!m_print_config.cooling.get_at(0) || min_layer_time <= 0.0) {
        return;
    }

    // calculate the fixed (travels) and adjustable (extrusions) time of the layer
    double fixed_time = 0.0;
    double adjustable_time = 0.0;
    for (size_t i = layer_begin; i < m_toolpaths.size(); ++i) {
        const Aion::ToolpathEntity* const entity = m_toolpaths[i].get();
        const bool is_path = dynamic_cast<const Aion::ToolpathPath*>(entity) != nullptr;
        const Aion::ToolpathElements* const points = entity->getToolpathPoints();
        for (size_t j = 1; j < points->size(); ++j) {
            const double speed = (*points)[j].getSpeed() > 0.0 ? (*points)[j].getSpeed() : entity->speed;
            if (speed <= 0.0) {
                continue;
            }
            const double time = (*points)[j - 1].getPoint().DistanceTo((*points)[j].getPoint()) / speed;
            if (is_path) {
                adjustable_time += time;
            }
            else {
                fixed_time += time;
            }
        }
    }
    // skip if there is no slow down needed or not possible
    if (adjustable_time <= 0.0 || fixed_time + adjustable_time >= min_layer_time || min_layer_time - fixed_time <= 0.0) {
        return;
    }
    // Adjust the speed to not exceed the minimum layer time
    const double factor = adjustable_time / (min_layer_time - fixed_time);
    const double min_speed = m_print_config.min_print_speed.get_at(0);
    for (size_t i = layer_begin; i < m_toolpaths.size(); ++i) {
        if (dynamic_cast<const Aion::ToolpathPath*>(m_toolpaths[i].get()) == nullptr) {
            continue;
        }
        for (Aion::ToolpathElement& point : *m_toolpaths[i]->getToolpathPoints()) {
            const double speed = point.getSpeed();
            double new_speed = speed * factor;
            // limit the speed to min_speed
            if (new_speed < min_speed) {
                new_speed = std::min(speed, min_speed);
            }
            point.setSpeed(new_speed);
        }
    }
}

bool CamSlicer::generateToolpath(Aion::Errorlist& errors)
{
    //clear existing paths
    m_toolpaths.clear();

    if (m_tool->getString().empty()) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "No tool specified"));
        // No valid result, return false -> gcode export not possible
        return false;
    }

    const Aion::ToolConfig* tool_config = getConfig()->getTool(m_tool->getString());
    if (!tool_config) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Critical, "Tool not found in config"));
        return false;
    }

    if (m_speed->getFloat() <= 0.0) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Print speed must be greater than zero."));
        return false;
    }

    // Check if there are components
    if (m_volume_components->getComponents()->empty()) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Info, "No volume selected"));
        return true;
    }

    // orientation vector valid?
    ON_3dVector dir = m_orientation_vector->getVector();
    if (!dir.Unitize()) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Orientation vector must not be zero."));
        return false;
    }

    // check height must be lower than the width, otherwise the slicer will fail
    if (m_height->getFloat() >= m_width->getFloat() || m_first_layer_height->getFloat() >= m_width->getFloat()) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Slicing not possible: Layer height must be lower than the extrusion width."));
        return false;
    }

    // Mesh all the selected objects and collect the ON_Meshes
    std::vector<std::unique_ptr<ON_Mesh>> on_meshes;
    on_meshes.reserve(m_volume_components->getComponents()->size());
    for (const Aion::Component& comp : *m_volume_components->getComponents()) {
        std::unique_ptr<ON_Mesh> on_mesh = Aion::Geometry5X::createNurbsMesh(comp, errors, ON_MeshParameters::DefaultAnalysisMesh);
        if (on_mesh) {
            on_meshes.emplace_back(std::move(on_mesh));
        }
        else {
            return false; // error already reported in createNurbsMesh
        }
    }

    // Rotate the slicing direction to +Z and move the bottom center of the rotated bounding box to the print bed center
    ON_Xform rot_xform;
    rot_xform.Rotation(dir, ON_3dVector::ZAxis, ON_3dPoint::Origin);
    // bounding box of the rotated meshes
    ON_BoundingBox rot_bbox;
    for (const std::unique_ptr<ON_Mesh>& on_mesh_ptr : on_meshes) {
        on_mesh_ptr->GetTightBoundingBox(rot_bbox, true, &rot_xform);
    }
    ON_3dPoint point = rot_bbox.Center();
    point.z = rot_bbox.Min().z;
    // scene to slicer transform
    const ON_Xform to_slicer = ON_Xform::TranslationTransformation(ON_3dPoint::Origin - point) * rot_xform;
    // slicer to scene transform
    const ON_Xform to_scene = to_slicer.Inverse();

    // Create a Slic3r model and print
    Slic3r::Model s_model;
    Slic3r::Print s_print;
    Slic3r::ModelObject* s_mo_ptr = s_model.add_object();
    // generic name for PrusaSlicer messages
    s_mo_ptr->name = "\"Slicer volume\"";
    s_mo_ptr->add_instance();
    // Get create the slicer config from the initial config and the options of this cam block
    Slic3r::DynamicPrintConfig s_config;
    if (!loadSlicerConfig(s_config, errors)) {
        return false;
    }
    // reject invalid values, e.g. from the user configuration options
    if (const std::string config_error = s_config.validate(); !config_error.empty()) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Invalid slicer configuration: " + config_error));
        return false;
    }
    // Apply the config to the m_print_config
    m_print_config.apply(s_config, true);
    // Aion prints with one tool, Warning if multiple tools are configured in the slicer config.
    if (m_print_config.nozzle_diameter.size() > 1) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Slicing not possible: Multiple tools are configured, only one tool is supported."));
        return false;
    }
    // inform about options that are changed from the PrusaSlicer default, but only act in PrusaSlicer's G-code export.
    // s_config is used, because the filament overrides are not part of m_print_config.
    for (const auto& [reason, keys] : UNSUPPORTED_OPTIONS) {
        std::string changed_keys;
        for (const char* key : keys) {
            const Slic3r::ConfigOption* s_option = s_config.option(key);
            const Slic3r::ConfigOptionDef* s_def = Slic3r::print_config_def.get(key);
            if (!s_option || !s_def || !s_def->default_value) {
                continue;
            }
            // nullable options (filament overrides) are only applied by PrusaSlicer if they are not nil
            const bool changed = s_option->nullable() ? !s_option->is_nil() : !(*s_option == *s_def->default_value);
            if (changed) {
                changed_keys += (changed_keys.empty() ? "" : ", ") + std::string(key);
            }
        }
        if (!changed_keys.empty()) {
            errors.push_back(Aion::Error(Aion::ErrorClass::Info, "Slicer options " + changed_keys + " " + reason + "."));
        }
    }

    // Create slicer Meshes from the ON_Meshes and add them to the Slic3r model
    for (const std::unique_ptr<ON_Mesh>& on_mesh_ptr : on_meshes) {
        Slic3r::TriangleMesh s_mesh;
        if (!_createSlicerMesh(*on_mesh_ptr, to_slicer, s_mesh, errors)) {
            return false;
        }
        s_mo_ptr->add_volume(std::move(s_mesh));
    }

    // Set the print model and config
    s_print.apply(s_model, s_config);
    // Validate the print config before slicing
    if (const std::string print_error = s_print.validate(); !print_error.empty()) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Slicing not possible: " + print_error));
        return false;
    }
    // set the callback to check for user abort during slicing
    s_print.set_status_callback([this, &s_print](const Slic3r::PrintBase::SlicingStatus&) {
        if (isPlanningAborted()) {
            s_print.cancel();
        }
    });
    Slic3r::Seams::Placer seam_placer;
    const bool use_seam_placer = !m_print_config.spiral_vase.value;
    // slice the print
    try {
        s_print.process();
        // init seam placer after slicing (only if spiral vase mode is not used)
        if (use_seam_placer) {
            seam_placer.init(s_print.objects(), Slic3r::Seams::Placer::get_params(s_print.full_print_config()), []() {});
        }
    }
    catch (const Slic3r::CanceledException&) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Critical, "Pathplanning aborted by user."));
        return false;
    }
    catch (const std::exception& exc) {
        errors.push_back(Aion::Error(Aion::ErrorClass::Critical, std::string("Slicing failed: ") + exc.what()));
        return false;
    }

    // Create a tool ordering for the print (needed also with one tool)
    const Slic3r::ToolOrdering s_tool_ordering(s_print, 0, false);
    const double nozzle_diameter = m_print_config.nozzle_diameter.get_at(0);
    // get the seam gap parameters
    const Slic3r::FloatOrPercent s_seam_gap_override = m_print_config.filament_seam_gap_distance.get_at(0);
    const double seam_gap = std::isnan(s_seam_gap_override.value) ? m_print_config.seam_gap_distance.get_abs_value(nozzle_diameter) : s_seam_gap_override.get_abs_value(nozzle_diameter);
    // create the path smoothing function for the extrusion ordering
    const Slic3r::GCode::ExtrusionOrder::PathSmoothingFunction s_smooth_path = SlicerPathSmoothing{
        use_seam_placer ? &seam_placer : nullptr,
        Slic3r::scaled<double>(m_print_config.gcode_resolution.value),
        use_seam_placer ? Slic3r::scaled<double>(seam_gap) : 0.0
    };
    // end point of the last extrusion
    // std::optional is used because Slic3r::Point has no invalid state.
    std::optional<Slic3r::Point> last_pos;
    size_t skirt_layers_done = 0;
    bool brim_done = false;

    //iterate over objects in print and layers in object to generate toolpaths
    for (const Slic3r::PrintObject* s_po : s_print.objects()) {
        const std::vector<Slic3r::GCode::InstanceToPrint> s_instances{ Slic3r::GCode::InstanceToPrint(0, *s_po, 0) };
        size_t layer_idx = 0;
        size_t support_idx = 0;
        while (layer_idx < s_po->layers().size() || support_idx < s_po->support_layers().size()) {
            // check for user abort during slicing
            if (isPlanningAborted()) {
                errors.push_back(Aion::Error(Aion::ErrorClass::Critical, "Pathplanning aborted by user."));
                m_toolpaths.clear();
                return false;
            }
            // pair the object and support layer with the same print_z for the extrusion ordering
            Slic3r::GCode::ObjectLayerToPrint s_layer_to_print;
            s_layer_to_print.object_layer = layer_idx < s_po->layers().size() ? s_po->layers()[layer_idx] : nullptr;
            s_layer_to_print.support_layer = support_idx < s_po->support_layers().size() ? s_po->support_layers()[support_idx] : nullptr;
            if (s_layer_to_print.object_layer && s_layer_to_print.support_layer) {
                if (s_layer_to_print.object_layer->print_z < s_layer_to_print.support_layer->print_z - EPSILON) {
                    s_layer_to_print.support_layer = nullptr;
                }
                else if (s_layer_to_print.support_layer->print_z < s_layer_to_print.object_layer->print_z - EPSILON) {
                    s_layer_to_print.object_layer = nullptr;
                }
            }
            if (s_layer_to_print.object_layer) {
                ++layer_idx;
            }
            if (s_layer_to_print.support_layer) {
                ++support_idx;
            }

            // sorting by tools is needed even if there is only one tool.
            const Slic3r::LayerTools& s_layer_tools = s_tool_ordering.tools_for_layer(s_layer_to_print.print_z());
            // nothing to extrude on this layer
            if (s_layer_tools.extruders.empty()) {
                continue;
            }

            // check if a skirt is needed on this layer
            std::map<unsigned int, std::pair<size_t, size_t>> s_skirt_loops;
            if (s_print.has_skirt() &&
                !s_print.skirt().empty() &&
                s_layer_tools.has_skirt &&
                (skirt_layers_done < size_t(m_print_config.skirt_height.value) || s_print.has_infinite_skirt())) {
                s_skirt_loops[s_layer_tools.extruders.front()] = { 0, s_print.skirt().entities.size() };
                ++skirt_layers_done;
            }

            const Slic3r::GCode::ObjectsLayerToPrint s_layers{ s_layer_to_print };
            // Call get_extrusions to generate the extrusion order for this layer
            const std::vector<Slic3r::GCode::ExtrusionOrder::ExtruderExtrusions> s_extrusions = Slic3r::GCode::ExtrusionOrder::get_extrusions(
                s_print, nullptr, s_layers, s_layer_to_print.layer()->bottom_z() < slicerEPS, s_layer_tools, s_instances, s_skirt_loops,
                s_layer_tools.extruders.front(), s_smooth_path, !brim_done, last_pos);
            brim_done = true;

            // process the extrusions in print order and generate the toolpath points
            processSortedExtrusions(s_extrusions, s_layer_to_print, tool_config, to_scene, dir, last_pos);
        }
    }

    // generate the wiping of the last path
    if (!m_toolpaths.empty()) {
        if (Aion::ToolpathPath* const last_path = dynamic_cast<Aion::ToolpathPath*>(m_toolpaths.back().get())) {
            this->generateWiping(last_path, false);
        }
    }
    else {
        errors.push_back(Aion::Error(Aion::ErrorClass::Info, "No extrusions generated, the object may be too small for the extrusion width / layer height."));
    }

    return true;
}

// TODO: Implement icon for CamSlicer without wxWidgets dependency.
/* 
HICON CamVolume::getIcon(int size_x, int size_y) const
{
    if (!m_icon.IsOk() || m_icon.GetSize() != wxSize(size_x, size_y)) {
        m_icon = Aion::loadPngResourceAsIcon("ICON_VOLUME_TOOL", wxSize(size_x, size_y));
    }
    return m_icon.GetHICON();
}
*/

Aion::OptionsGroups CamSlicer::getOptionsByGroup()
{
    Aion::OptionsGroups result;
    Aion::OptionsGroup group_tool;
    group_tool.name = "Tool";
    group_tool.options.push_back(m_tool);
    //const ToolConfig* tool_config = AionData().config.getTool(m_tool->getString());
    result.push_back(group_tool);

    if (!m_tool->getString().empty()) {
        Aion::OptionsGroup group_geometry;
        group_geometry.name = "Geometry";
        group_geometry.options.push_back(m_volume_components);
        group_geometry.options.push_back(m_orientation_vector);
        result.push_back(group_geometry);

        Aion::OptionsGroup group_extrusion;
        group_extrusion.name = "Extrusion parameters";
        group_extrusion.options.push_back(m_width);
        group_extrusion.options.push_back(m_height);
        group_extrusion.options.push_back(m_first_layer_height);
        group_extrusion.options.push_back(m_speed);
        group_extrusion.options.push_back(m_speed_factors);
        result.push_back(group_extrusion);

        Aion::OptionsGroup group_filling;
        group_filling.name = "Filling options";
        group_filling.options.push_back(m_perimeters);
        group_filling.options.push_back(m_perimeter_generator);
        group_filling.options.push_back(m_vase_mode);
        group_filling.options.push_back(m_top_layers);
        group_filling.options.push_back(m_bottom_layers);
        group_filling.options.push_back(m_infill_density);
        group_filling.options.push_back(m_infill_pattern);
        result.push_back(group_filling);

        Aion::OptionsGroup group_other;
        group_other.name = "Other";
        group_other.options.push_back(m_support);
        group_other.options.push_back(m_brim_width);
        group_other.options.push_back(m_travel_z_hop);
        group_other.options.push_back(m_slicer_options);
        result.push_back(group_other);
    }

    return result;
}

void CamSlicer::onOptionChanged(const Aion::ConfigOptionBase* option, bool reset)
{
    // set the recalculate flag
    setNeedsPathplanning(true);

    if (option == m_volume_components && !reset) {
        std::vector<Aion::Component>* components = m_volume_components->getComponents();
        // Select Volumes
        Aion::InteractiveSelection::interactiveVolumeSelection(*components);
    }

    if (option == m_orientation_vector && m_orientation_vector->pick_requested) {
        m_orientation_vector->pick_requested = false;
        // Select direction (surface normal or two points)
        ON_3dVector direction;
        if (Aion::InteractiveSelection::interactiveDirectionSelection(direction) == CRhinoGet::point) {
            *m_orientation_vector = direction;
        }
    }

    if (option == this->m_tool) {
        // When the tool was changed, we update need to set the default values
        const Aion::ToolConfig* tool_config = getConfig()->getTool(m_tool);
        const Aion::ToolConfig* last_tool_config = getConfig()->getTool(m_last_tool);
        bool force_update = m_last_tool->getString().empty(); // if no last tool was selected, we force the update

        if (tool_config) {
            // the first layer height follows the tool height unless the user changed it from the last tool's default
            bool update_first_layer = force_update || !last_tool_config ||
                fabs(m_first_layer_height->getFloat() - last_tool_config->tool_height()) < slicerEPS;

            // update options with order: height, width, speed, point feedrate
            updateToolPresets(tool_config, last_tool_config, force_update, m_height, m_width, m_speed, nullptr);

            if (update_first_layer) {
                *m_first_layer_height = tool_config->tool_height().getFloat();
            }
        }

        // set the last tool to the current tool
        m_last_tool->deserialize(m_tool->getString());
    }
}

void CamSlicer::onObjectChanged(const ON_UUID& uuid, ON::object_type type, bool deleted) {
    bool change = false;
    // check if the modified object is part of the volume components and if it is deleted we also delete it from the list
    for (auto it = m_volume_components->getComponents()->begin(); it != m_volume_components->getComponents()->end();) {
        if (it->uuid == uuid) {
            change = true;
            if (deleted) {
                it = m_volume_components->getComponents()->erase(it);
                continue; // continue to the next iteration
            }
        }
        it++;
    }

    // if we need to recalculate, we set the flag
    if (change) {
        setNeedsPathplanning(true);
    }
}
