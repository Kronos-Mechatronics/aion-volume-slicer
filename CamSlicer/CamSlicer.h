#pragma once
#include "stdafx.h"
#include "CamBase.h"

class CamSlicer : public Aion::CamBase
{
public:
    // initialize with default name
    CamSlicer(const ON_UUID& plugin_uuid);
    ~CamSlicer() = default;
    // override class information
    // name of this cam block type
    virtual std::string getTypeName() const override { return "Demo"; };
    // short description of this cam block type
    virtual std::string getTypeShortDescription() const override { return "CamSlicer example"; }
    // Optional url to documentation for this cam block type.
    virtual std::string getDocumentationUrl() const override { return "https://example.com"; }
    // factory method to create a new instance of this cam block type
    virtual Aion::CamBase* createNewInstance() const override;

    // generate Toolpath of the contour CAM block
    bool generateToolpath(Aion::Errorlist& errors) override;
    // Expose options of this cam block to UI
    Aion::OptionsGroups getOptionsByGroup() override;
    // callback for when an option was changed in the UI.
    void onOptionChanged(const Aion::ConfigOptionBase* option, bool reset = false) override;
    // Respond to modified geometry in the document
    void onObjectChanged(const ON_UUID& uuid, ON::object_type type, bool deleted) override;

private:
    // Config option for the tool
    Aion::ConfigOptionTool* m_tool;
    // Selected curve geometries
    Aion::ConfigOptionComponentVector* m_curves;
};