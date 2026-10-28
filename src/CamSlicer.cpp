#include "stdafx.h"
#include <filesystem>
#include "CamSlicer.h"
#include "InteractiveSelection.h"
#include "Geometry5X.h"

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
    m_orientation_vector->digits = 3;

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
    m_support = new Aion::ConfigOptionBool(false);
    this->m_options.addOption("support", m_support);
    m_support->label = "Support";
    m_support->tooltip = "Generate supports for this CAM-Block.\nOnly use for structures on the printbed.";

    m_brim_width = new Aion::ConfigOptionFloat(0.0);
    this->m_options.addOption("brimWidth", m_brim_width);
    m_brim_width->label = "Brim";
    m_brim_width->tooltip = "Width of the brim around the first layer. 0 disables the brim.\nOnly use for structures on the printbed.";
    m_brim_width->unit = "mm";
    m_brim_width->min = 0.0;
    m_brim_width->detail_level = Aion::ConfigDetailLevel::levelTwo;

    m_slicer_options = new Aion::ConfigOptionString();
    this->m_options.addOption("slicerOptions", m_slicer_options);
    m_slicer_options->label = "Configuration options";
    m_slicer_options->tooltip = "Additional PrusaSlicer options in \".ini\" format (one \"key = value\" per line).\nThey are applied last and overwrite all other settings.";
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
    
    s_config.set_deserialize_strict({
        { "layer_height", m_height->getFloat() },
        { "first_layer_height", m_first_layer_height->getFloat() },
    });

    // all speeds are absolute values relative to the print speed
    const double speed = m_speed->getFloat() / 100.0;
    s_config.set_deserialize_strict({
        { "perimeter_speed", speed * m_perimeter_speed->getFloat() },
        { "small_perimeter_speed", speed * m_small_perimeter_speed->getFloat() },
        { "external_perimeter_speed", speed * m_external_perimeter_speed->getFloat() },
        { "infill_speed", speed * m_infill_speed->getFloat() },
        { "solid_infill_speed", speed * m_solid_infill_speed->getFloat() },
        { "top_solid_infill_speed", speed * m_top_solid_infill_speed->getFloat() },
        { "support_material_speed", speed * m_support_speed->getFloat() },
        { "support_material_interface_speed", speed * m_support_interface_speed->getFloat() },
        { "bridge_speed", speed * m_bridge_speed->getFloat() },
        { "over_bridge_speed", speed * m_over_bridge_speed->getFloat() },
        { "gap_fill_speed", speed * m_gap_fill_speed->getFloat() },
    });

    s_config.set_deserialize_strict({
        { "perimeters", m_perimeters->getInt() },
        { "perimeter_generator", m_perimeter_generator->getInt() == PerimeterGenerator::classic ? "classic" : "arachne" },
        { "top_solid_layers", m_top_layers->getInt() },
        { "bottom_solid_layers", m_bottom_layers->getInt() },
        { "fill_density", m_infill_density->serialize() },
        { "fill_pattern", INFILL_PATTERNS.at(m_infill_pattern->getInt()).second },
        { "support_material", m_support->getBool() },
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

    applyConfigOptionsToSlicerConfig(s_config);

    // user defined options overwrite everything else
    if (!m_slicer_options->getString().empty()) {
        try {
            s_config.load_from_ini_string(m_slicer_options->getString(), Slic3r::ForwardCompatibilitySubstitutionRule::EnableSilent);
        }
        catch (const std::exception& exc) {
            errors.push_back(Aion::Error(Aion::ErrorClass::Warning, std::string("Could not parse configuration options: ") + exc.what()));
            return false;
        }
    }
    return true;
}

// Generate the Slic3r TriangleMesh from the Rhino ON_Mesh
// @param on_mesh: pointer to the Rhino ON_Mesh to convert
// @return unique pointer to the Slic3r TriangleMesh
std::unique_ptr<Slic3r::TriangleMesh> _createSlicerMesh(const ON_Mesh* on_mesh) {
    ON_3fPointArray on_vertices = on_mesh->m_V;
    std::vector<Slic3r::Vec3f> s_vertices;
    s_vertices.reserve(on_vertices.Count());
    for (int i = 0; i < on_vertices.Count(); i++) {
        ON_3fPoint* on_vert = on_vertices.At(i);
        s_vertices.emplace_back(Slic3r::Vec3f(on_vert->x, on_vert->y, on_vert->z));
    }

    ON_SimpleArray<ON_MeshFace> on_faces = on_mesh->m_F;
    std::vector<Slic3r::Vec3i> s_faces;
    s_faces.reserve(on_faces.Count());
    for (int i = 0; i < on_faces.Count(); i++) {
        ON_MeshFace* on_face = on_faces.At(i);
        s_faces.emplace_back(Slic3r::Vec3i(on_face->vi[0], on_face->vi[1], on_face->vi[2]));
    }

    return std::make_unique<Slic3r::TriangleMesh>(s_vertices, s_faces);
}

// Get the speed for a given extrusion role from the Slic3r DynamicPrintConfig
// @param s_role: Slic3r ExtrusionRole to get the speed for
// @param is_first_layer: whether the path is on the first layer
// @param s_config: Slic3r DynamicPrintConfig to get the speeds from
// @return the speed for the given extrusion role
double _getSpeedForExtrusionRole(Slic3r::ExtrusionRole& s_role, bool is_first_layer, const Slic3r::DynamicPrintConfig& s_config) {
    double speed;
    if (s_role == Slic3r::ExtrusionRole::Perimeter) {
        speed = s_config.get_abs_value("perimeter_speed");
    }
    else if (s_role == Slic3r::ExtrusionRole::ExternalPerimeter) {
        speed = s_config.get_abs_value("external_perimeter_speed");
    }
    else if (s_role.is_bridge()) {
        assert(s_role.is_perimeter() || s_role == Slic3r::ExtrusionRole::BridgeInfill);
        speed = s_config.get_abs_value("bridge_speed");
    }
    else if (s_role == Slic3r::ExtrusionRole::InternalInfill) {
        speed = s_config.get_abs_value("infill_speed");
    }
    else if (s_role == Slic3r::ExtrusionRole::SolidInfill) {
        speed = s_config.get_abs_value("solid_infill_speed");
    }
    else if (s_role == Slic3r::ExtrusionRole::SupportMaterial) {
        speed = s_config.get_abs_value("support_material_speed");
    }
    else if (s_role == Slic3r::ExtrusionRole::SupportMaterialInterface) {
        speed = s_config.get_abs_value("support_material_interface_speed");
    }
    else if (s_role == Slic3r::ExtrusionRole::Skirt) {
        speed = s_config.get_abs_value("support_material_speed");
    }
    else if (s_role == Slic3r::ExtrusionRole::InfillOverBridge) {
        const double solid_infill_speed = s_config.get_abs_value("solid_infill_speed");
        const double over_bridge_speed{ s_config.get_abs_value("over_bridge_speed", solid_infill_speed) };
        if (over_bridge_speed > 0) {
            speed = over_bridge_speed;
        }
        else {
            speed = solid_infill_speed;
        }
    }
    else if (s_role == Slic3r::ExtrusionRole::TopSolidInfill) {
        speed = s_config.get_abs_value("top_solid_infill_speed");
    }
    else if (s_role == Slic3r::ExtrusionRole::Ironing) {
        speed = s_config.get_abs_value("ironing_speed");
    }
    else if (s_role == Slic3r::ExtrusionRole::GapFill) {
        speed = s_config.get_abs_value("gap_fill_speed");
    }
    else {
        throw Slic3r::InvalidArgument("Invalid speed");
    }
    if (is_first_layer) {
        const double first_layer_infill_speed{ s_config.get_abs_value("first_layer_infill_speed", speed) };
        if (s_role == Slic3r::ExtrusionRole::SolidInfill && first_layer_infill_speed > 0) {
            speed = first_layer_infill_speed;
        }
        else {
            speed = s_config.get_abs_value("first_layer_speed", speed);
        }
    }
    return speed;
}

// Create a ToolpathPath from a Slic3r ExtrusionPath
// @param path_ptr: pointer to the ToolpathPath to fill
// @param s_path_ptr: pointer to the Slic3r ExtrusionPath to convert
// @param path_z: Z height of the path
// @param transform: transformation to apply to the points
// @param dir: direction vector to apply to the points
// @param s_config: Slic3r DynamicPrintConfig to get the speeds from
void _createToolpathForSlicerpath(Aion::ToolpathPath* const path_ptr, const Slic3r::ExtrusionPath* s_path_ptr, double path_z, const ON_Xform& transform, const ON_3dVector& dir, const Slic3r::DynamicPrintConfig& s_config) {
    double s_path_height = s_path_ptr->height();
    double s_path_width = s_path_ptr->width();
    double s_path_volume = s_path_ptr->mm3_per_mm();

    for (const Slic3r::Point& s_point : s_path_ptr->polyline) {
        ON_3dPoint on_point = ON_Point(Slic3r::unscale<double>(s_point.x()), Slic3r::unscale<double>(s_point.y()), path_z);
        on_point.Transform(transform);
        if (!path_ptr->hasToolpathPoints() || path_ptr->getLastPoint()->getPoint() != on_point) {
            path_ptr->appendPoint(on_point, dir);
        }
        path_ptr->getLastPoint()->setWidth(s_path_width, s_path_height);
        path_ptr->getLastPoint()->setmm3_per_mm(s_path_volume);
        bool is_first_layer = false;
        if (path_z < 0.01) {
            is_first_layer = true;
        }
        if (s_path_ptr->role().is_perimeter() && s_path_ptr->length() <= SMALL_PERIMETER_LENGTH) {
            path_ptr->getLastPoint()->setSpeed(s_config.get_abs_value("small_perimeter_speed", s_config.get_abs_value("perimeter_speed")));
        }
        else {
            path_ptr->getLastPoint()->setSpeed(_getSpeedForExtrusionRole(s_path_ptr->role(), is_first_layer, s_config));
        }
    }
}

void CamSlicer::processEntityCollection(const Slic3r::ExtrusionEntityCollection& s_collection, double path_z, const Aion::ToolConfig* tool_config, const ON_Xform& transform, const ON_3dVector& dir, const Slic3r::DynamicPrintConfig& s_config)
{
    //iterate the extrusion entities of the collection
    for (Slic3r::ExtrusionEntity* s_entity : s_collection) {
        Aion::ToolpathPath* const path_ptr = new Aion::ToolpathPath(tool_config);
        path_ptr->speed = m_speed->getFloat();
        if (isExtrusionPath(s_entity)) {
            const Slic3r::ExtrusionPath* s_path_ptr = dynamic_cast<const Slic3r::ExtrusionPath*>(s_entity);
            _createToolpathForSlicerpath(path_ptr, s_path_ptr, path_z, transform, dir, s_config);
        }
        else if (isExtrusionMultiPath(s_entity)) {
            const Slic3r::ExtrusionMultiPath* s_mpath_ptr = dynamic_cast<const Slic3r::ExtrusionMultiPath*>(s_entity);
            for (const Slic3r::ExtrusionPath& s_path : s_mpath_ptr->paths) {
                _createToolpathForSlicerpath(path_ptr, &s_path, path_z, transform, dir, s_config);
            }
        }
        else if (isExtrusionLoop(s_entity)) {
            const Slic3r::ExtrusionLoop* s_loop_ptr = dynamic_cast<const Slic3r::ExtrusionLoop*>(s_entity);
            for (const Slic3r::ExtrusionPath& s_path : s_loop_ptr->paths) {
                _createToolpathForSlicerpath(path_ptr, &s_path, path_z, transform, dir, s_config);
            }
        }
        else {
            RhinoApp().Print("Unhandled ExtrusionEntityType encountered at layer height: %f.\n", path_z);
        }

        this->generateWiping(path_ptr, true);
        this->generateWiping(path_ptr, false);

        if (!m_toolpaths.empty() && m_toolpaths.back()->hasToolpathPoints() && path_ptr->hasToolpathPoints()) {
            Aion::ToolpathElement& last = m_toolpaths.back()->getToolpathPoints()->back();
            if (last.getPoint().DistanceTo(path_ptr->getFirstPoint()->getPoint()) > slicerEPS) {
                Aion::ToolpathTravel* travel = new Aion::ToolpathTravel(tool_config);
                ON_3dPoint from_p = last.getPoint();
                ON_3dVector from_v = last.getNormalIn();
                travel->appendPoint(from_p, from_v);
                from_v.Unitize();
                from_p.Transform(ON_Xform::TranslationTransformation(from_v));
                travel->appendPoint(from_p, last.getNormalIn());
                ON_3dPoint to_p = path_ptr->getFirstPoint()->getPoint();
                ON_3dVector to_v = path_ptr->getFirstPoint()->getNormalIn();
                to_v.Unitize();
                to_p.Transform(ON_Xform::TranslationTransformation(to_v));
                travel->appendPoint(to_p, path_ptr->getFirstPoint()->getNormalIn());
                travel->appendPoint(path_ptr->getFirstPoint()->getPoint(), path_ptr->getFirstPoint()->getNormalIn());

                travel->height = last.getHeight();
                travel->getLastPoint()->setWidth(path_ptr->getFirstPoint()->getWidth(), path_ptr->getFirstPoint()->getHeight());

                travel->speed = getConfig()->machineConfig().travel_feedrate();
                m_toolpaths.emplace_back(travel);
            }
        }
        m_toolpaths.emplace_back(path_ptr);
    }
}

void CamSlicer::createFlatCollectionFromLayer(const Slic3r::Layer* s_layer, Slic3r::ExtrusionEntityCollection& collection)
{
    const Slic3r::SupportLayer* sup_layer = dynamic_cast<const Slic3r::SupportLayer*>(s_layer);
    if (sup_layer) {
        collection = sup_layer->support_fills.flatten();
    }
    else {
        Slic3r::ExtrusionEntityCollection tmp;
        for (Slic3r::LayerRegion* region : s_layer->regions()) {
            tmp.append(region->perimeters().flatten());
            tmp.append(region->fills().flatten());
        }
        collection = tmp.flatten();
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

    // Compute the bounding box of all meshes and calculate the rotation and translation place them on the print bed.
    ON_BoundingBox init_bbox;
    for (std::unique_ptr<ON_Mesh>& on_mesh_ptr : on_meshes) {
        init_bbox.Union(on_mesh_ptr->BoundingBox());
    }
    ON_Xform rot_xform;
    rot_xform.Rotation(dir, ON_3dVector::ZAxis, init_bbox.Center());
    ON_BoundingBox rot_bbox;
    for (std::unique_ptr<ON_Mesh>& on_mesh_ptr : on_meshes) {
        on_mesh_ptr->Transform(rot_xform);
        rot_bbox.Union(on_mesh_ptr->BoundingBox());
    }
    ON_3dPoint point = rot_bbox.Center();
    point.z = rot_bbox.Min().z;
    ON_Xform transform = ON_Xform::TranslationTransformation(ON_3dPoint::Origin - point) * rot_xform;
    transform.Invert();

    try {
        // Create a Slic3r model and print
        Slic3r::Model s_model;
        Slic3r::Print s_print;
        Slic3r::ModelObject* s_mo_ptr = s_model.add_object();
        s_mo_ptr->add_instance();
        // Get create the slicer config from the initial config and the options of this cam block
        Slic3r::DynamicPrintConfig s_config;
        if (!loadSlicerConfig(s_config, errors)) {
            return false;
        }
        
        // Create slicer Meshes from the ON_Meshes and add them to the Slic3r model
        for (std::unique_ptr<ON_Mesh>& on_mesh_ptr : on_meshes) {
            on_mesh_ptr->Translate(ON_3dPoint::Origin - point);
            auto s_mesh_ptr = _createSlicerMesh(on_mesh_ptr.get());
            s_mo_ptr->add_volume(*s_mesh_ptr.get());
        }

        // Process the Slic3r model to generate the toolpaths
        s_print.apply(s_model, s_config);
        s_print.process();

        // Generate possible skirt and brim paths
        if (s_print.has_skirt()) {
            processEntityCollection(s_print.skirt().flatten(), 0, tool_config, transform, dir, s_config);
        }
        if (s_print.has_brim()) {
            processEntityCollection(s_print.brim().flatten(), 0, tool_config, transform, dir, s_config);
        }

        //iterate over objects in print and layers in object to generate toolpaths
        for (const Slic3r::PrintObject* s_po : s_print.objects()) {
            const Slic3r::SupportLayer* const* sup_it = s_po->support_layers().begin();
            const Slic3r::SupportLayer* const* sup_end = s_po->support_layers().end();
            double sup_z = INT_MAX;
            if (!sup_it) {
                sup_it = nullptr;
                sup_end = nullptr;
            }
            const Slic3r::Layer* const* lay_it = s_po->layers().begin();
            const Slic3r::Layer* const* lay_end = s_po->layers().end();
            while (sup_it != sup_end || lay_it != lay_end) {
                // set the z height of the next support layer, if there is one, otherwise set it to INT_MAX
                if (sup_it && sup_it != sup_end) {
                    sup_z = (*sup_it)->bottom_z();
                }
                else {
                    sup_z = INT_MAX;
                }
                Slic3r::ExtrusionEntityCollection col;
                // process the next layer, either support or normal, depending on which one has the lower z height
                if (sup_z <= (*lay_it)->bottom_z()) {
                    createFlatCollectionFromLayer(*sup_it, col);
                    processEntityCollection(col, (*sup_it)->bottom_z(), tool_config, transform, dir, s_config);
                    ++sup_it;
                }
                else {
                    createFlatCollectionFromLayer(*lay_it, col);
                    processEntityCollection(col, (*lay_it)->bottom_z(), tool_config, transform, dir, s_config);
                    ++lay_it;
                }
            }
        }

    }
    catch (const std::exception& exc) {
        RhinoApp().Print(exc.what());
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
                *m_first_layer_height = tool_config->tool_height();
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
