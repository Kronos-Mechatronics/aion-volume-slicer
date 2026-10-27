#include "stdafx.h"
#include <filesystem>
#include "CamSlicer.h"
#include "InteractiveSelection.h"
#include "Geometry5X.h"

enum SlicerConfigMode : unsigned char {
    path_mode,
    string_mode
};

// Helper function instead of static data to avoid delay-load issues from Aion-5X.rhp
inline const std::map<std::string, int>& GetConfigModeStringMap() {
    static const std::map<std::string, int> map{
        {"Path", path_mode},
        {"String", string_mode}
    };
    return map;
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

    m_slicer_config_mode = new Aion::ConfigOptionEnum(0, &GetConfigModeStringMap());
    this->m_options.addOption("configMode", m_slicer_config_mode);
    m_slicer_config_mode->label = "Config mode";
    m_slicer_config_mode->tooltip = "Select the config mode for the Slicer \n Path: Use the \".ini\" config file (absolute path) \n String: Use the config file string";

    m_slicer_config_path = new Aion::ConfigOptionString();
    this->m_options.addOption("slicerConfigPath", m_slicer_config_path);
    m_slicer_config_path->label = "Config path";
    m_slicer_config_path->tooltip = "Path to Slicer config \".ini\" file";

    m_slicer_config_string = new Aion::ConfigOptionString();
    this->m_options.addOption("slicerConfigString", m_slicer_config_string);
    m_slicer_config_string->label = "Config string";
    m_slicer_config_string->tooltip = "Slicer config string from \".ini\" file";
    m_slicer_config_string->multiline = true;

    m_last_slicer_config_path = new Aion::ConfigOptionString();
    this->m_options.addOption("lastSlicerConfig", m_last_slicer_config_path);
    m_last_slicer_config_path->detail_level = Aion::ConfigDetailLevel::levelInvisible;

    m_support = new Aion::ConfigOptionBool();
    this->m_options.addOption("support", m_support);
    m_support->label = "Generate supports";
    m_support->tooltip = "Should supports be generated for objects in this CAM-Block?\nOnly use for structures on the printbed.";

    m_brim = new Aion::ConfigOptionBool();
    this->m_options.addOption("brim", m_brim);
    m_brim->label = "Generate brim";
    m_brim->tooltip = "Should a brim be generated for objects in this CAM-Block?\nOnly use for structures on the printbed.";

    m_skirt = new Aion::ConfigOptionBool();
    this->m_options.addOption("skirt", m_skirt);
    m_skirt->label = "Generate skirt";
    m_skirt->tooltip = "Should a skirt be generated for objects in this CAM-Block?\nOnly use for structures on the printbed.";
}

Aion::CamBase* CamSlicer::createNewInstance() const
{
    CamSlicer* result = new CamSlicer(this->getPluginUuid());
    return result;
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

    if (m_slicer_config_mode->getInt() == SlicerConfigMode::path_mode) {
        std::filesystem::path path(m_slicer_config_path->getString());
        if (m_slicer_config_path->getString().empty()) {
            errors.push_back(Aion::Error(Aion::ErrorClass::Info, "No config file selected. A standard config will be used."));
        }
        else {
            if (!path.is_absolute() || path.filename().string().find(".ini") == -1) {
                errors.push_back(Aion::Error(Aion::ErrorClass::Warning, "Path is no absolute path to \".ini\" file. A standard config will be used."));
            }
        }
    }

    if (m_slicer_config_mode->getInt() == SlicerConfigMode::string_mode) {
        if (m_slicer_config_string->getString().empty()) {
            errors.push_back(Aion::Error(Aion::ErrorClass::Info, "No config string given. A standard config will be used."));
        }
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

    try {
        Slic3r::Model s_model;
        Slic3r::Print s_print;

        Slic3r::ModelObject* s_mo_ptr = s_model.add_object();
        s_mo_ptr->add_instance();

        Slic3r::DynamicPrintConfig s_config = Slic3r::DynamicPrintConfig::full_print_config();

        std::filesystem::path path(m_slicer_config_path->getString());
        if (m_slicer_config_mode->getInt() == SlicerConfigMode::path_mode &&
            path.is_absolute() && path.filename().string().find(".ini") != -1) {
            s_config.load_from_ini(m_slicer_config_path->getString(), Slic3r::ForwardCompatibilitySubstitutionRule::EnableSilent);
        }
        if (m_slicer_config_mode->getInt() == SlicerConfigMode::string_mode &&
            !m_slicer_config_string->getString().empty()) {
            s_config.load_from_ini_string(m_slicer_config_string->getString(), Slic3r::ForwardCompatibilitySubstitutionRule::EnableSilent);
        }

        if (m_support->getBool() == false) {
            //auto* opt = s_config.option("support_material", false);
            //opt->set(new Slic3r::ConfigOptionBool(false));
            //s_config.set_deserialize("support_material", "false");
            s_config.set_key_value("support_material", new Slic3r::ConfigOptionBool(false)) ? RhinoApp().Print(L"sup key created\n") : RhinoApp().Print(L"sup key overwritten\n");
        }

        if (m_brim->getBool() == false) {
            s_config.set_key_value("brim_type", new Slic3r::ConfigOptionEnum<Slic3r::BrimType>(Slic3r::BrimType::btNoBrim)) ? RhinoApp().Print(L"brim key created\n") : RhinoApp().Print(L"brim key overwritten\n");
        }

        if (m_skirt->getBool() == false) {
            s_config.set_key_value("skirts", new Slic3r::ConfigOptionInt(0)) ? RhinoApp().Print(L"skirt key created\n") : RhinoApp().Print(L"skirt key overwritten\n");
        }

        std::vector<std::unique_ptr<ON_Mesh>> on_meshes;
        on_meshes.reserve(m_volume_components->getComponents()->size());
        for (Aion::Component& comp : *m_volume_components->getComponents()) {
            on_meshes.emplace_back(createNurbsMesh(comp));
        }

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

        for (std::unique_ptr<ON_Mesh>& on_mesh_ptr : on_meshes) {
            on_mesh_ptr->Translate(ON_3dPoint::Origin - point);
            auto s_mesh_ptr = createSlicerMesh(on_mesh_ptr.get());
            s_mo_ptr->add_volume(*s_mesh_ptr.get());
        }

        s_print.apply(s_model, s_config);
        s_print.process();

        if (s_print.has_skirt()) {
            processEntityCollection(s_print.skirt().flatten(), 0, tool_config, transform, dir, s_config);
        }

        if (s_print.has_brim()) {
            processEntityCollection(s_print.brim().flatten(), 0, tool_config, transform, dir, s_config);
        }

        //iterate over objects in print
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
                if (sup_it && sup_it != sup_end) {
                    sup_z = (*sup_it)->bottom_z();
                }
                else {
                    sup_z = INT_MAX;
                }
                Slic3r::ExtrusionEntityCollection col;
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
        Aion::OptionsGroup group_slicer;
        group_slicer.name = "Slicer";
        group_slicer.options.push_back(m_slicer_config_mode);
        if (m_slicer_config_mode->getInt() == SlicerConfigMode::path_mode) {
            group_slicer.options.push_back(m_slicer_config_path);
        }
        if (m_slicer_config_mode->getInt() == SlicerConfigMode::string_mode) {
            group_slicer.options.push_back(m_slicer_config_string);
        }
        group_slicer.options.push_back(m_support);
        group_slicer.options.push_back(m_brim);
        group_slicer.options.push_back(m_skirt);
        result.push_back(group_slicer);

        Aion::OptionsGroup group_geometry;
        group_geometry.name = "Geometry";
        group_geometry.options.push_back(m_volume_components);
        group_geometry.options.push_back(m_orientation_vector);
        result.push_back(group_geometry);
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

std::unique_ptr<ON_Mesh> CamSlicer::createNurbsMesh(Aion::Component& comp, ON_MeshParameters mesh_params) {
    auto on_mesh = std::make_unique<ON_Mesh>();
    const CRhinoObject* r_object = CRhinoObject::FromId(0, comp.uuid);
    const ON_Geometry* on_ptr = r_object->Geometry();
    if (on_ptr->HasBrepForm()) {
        ON_Brep* on_brep_ptr = on_ptr->BrepForm();
        ON_SimpleArray<ON_Mesh*> on_meshes;
        int n_meshes = on_brep_ptr->CreateMesh(mesh_params, on_meshes);
        on_mesh->Append(n_meshes, on_meshes);
        on_mesh->ConvertQuadsToTriangles();
        ON_wString log;
        ON_TextLog textLog(log);
        if (!on_mesh->IsValid(&textLog)) {
            RhinoApp().Print(L"The mesh is not valid:\n%s\n", log.Array());
        }
    }
    else {
        RhinoApp().Print("This component has no ON_Brep representation:\n%s\n", comp.uuid);
    }
    return on_mesh;
}

std::unique_ptr<Slic3r::TriangleMesh> CamSlicer::createSlicerMesh(const ON_Mesh* on_mesh) {
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

void CamSlicer::createToolpathForSlicerpath(Aion::ToolpathPath* const path_ptr, const Slic3r::ExtrusionPath* s_path_ptr, double path_z, const ON_Xform& transform, const ON_3dVector& dir, const Slic3r::DynamicPrintConfig& s_config) {
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
            path_ptr->getLastPoint()->setSpeed(getSpeedForExtrusionRole(s_path_ptr->role(), is_first_layer, s_config));
        }
    }
}

double CamSlicer::getSpeedForExtrusionRole(Slic3r::ExtrusionRole& s_role, bool is_first_layer, const Slic3r::DynamicPrintConfig& s_config) {
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

void CamSlicer::processEntityCollection(const Slic3r::ExtrusionEntityCollection& s_collection, double path_z, const Aion::ToolConfig* tool_config, const ON_Xform& transform, const ON_3dVector& dir, const Slic3r::DynamicPrintConfig& s_config)
{
    //iterate the extrusion entities of the collection
    for (Slic3r::ExtrusionEntity* s_entity : s_collection) {
        Aion::ToolpathPath* const path_ptr = new Aion::ToolpathPath(tool_config);
        path_ptr->speed = tool_config->tool_speed();
        if (isExtrusionPath(s_entity)) {
            const Slic3r::ExtrusionPath* s_path_ptr = dynamic_cast<const Slic3r::ExtrusionPath*>(s_entity);
            createToolpathForSlicerpath(path_ptr, s_path_ptr, path_z, transform, dir, s_config);
        }
        else if (isExtrusionMultiPath(s_entity)) {
            const Slic3r::ExtrusionMultiPath* s_mpath_ptr = dynamic_cast<const Slic3r::ExtrusionMultiPath*>(s_entity);
            for (const Slic3r::ExtrusionPath& s_path : s_mpath_ptr->paths) {
                createToolpathForSlicerpath(path_ptr, &s_path, path_z, transform, dir, s_config);
            }
        }
        else if (isExtrusionLoop(s_entity)) {
            const Slic3r::ExtrusionLoop* s_loop_ptr = dynamic_cast<const Slic3r::ExtrusionLoop*>(s_entity);
            for (const Slic3r::ExtrusionPath& s_path : s_loop_ptr->paths) {
                createToolpathForSlicerpath(path_ptr, &s_path, path_z, transform, dir, s_config);
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