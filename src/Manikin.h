#pragma once

#include "amm_std.h"

#include "amm/Utility.h"

#include "amm/TopicNames.h"

#include <map>

#include "Net/Server.h"
#include "Net/Client.h"

#include <tinyxml2.h>

#include "bridge.h"

class Manikin : ListenerInterface {

protected:
    const std::string moduleName = "AMM_TCP_Bridge";
    const std::string config_file = "config/tcp_bridge_ajams.xml";

    std::map <std::string, AMM::EventRecord> eventRecords;
    std::string manikin_id;


    std::mutex m_mapmutex;

    std::map <std::string, std::map<std::string, double>> labNodes;

public:
    Manikin(std::string mid);

    void SetServer(Server* srv);

    ~Manikin();

    AMM::DDSManager <Manikin> *mgr;

    void ParseCapabilities(tinyxml2::XMLElement *node);

    void PublishSettings(std::string const &equipmentType);

    void HandleSettings(Client *c, std::string const &settingsVal);

    void HandleCapabilities(Client *c, std::string const &capabilityVal);

    void HandleStatus(Client *c, std::string const &statusVal);

    void DispatchRequest(Client *c, std::string const &request,
                         std::string mid = std::string());

    void PublishOperationalDescription();

    void PublishConfiguration();

    void InitializeLabNodes();

private:


    AMM::UUID m_uuid;

    Server* s;

    std::map <std::string, std::map<std::string, std::string>> equipmentSettings;

    const string capabilityPrefix = "CAPABILITY=";
    const string settingsPrefix = "SETTINGS=";
    const string statusPrefix = "STATUS=";
    const string configPrefix = "CONFIG=";
    const string modulePrefix = "MODULE_NAME=";
    const string registerPrefix = "REGISTER=";
    const string requestPrefix = "REQUEST=";
    const string keepHistoryPrefix = "KEEP_HISTORY=";
    const string actionPrefix = "ACT=";
    const string genericTopicPrefix = "[";
    const string keepAlivePrefix = "[KEEPALIVE]";
    const string loadScenarioPrefix = "LOAD_SCENARIO:";
    const string loadStatePrefix = "LOAD_STATE:";
    const string haltingString = "HALTING_ERROR";
    const string sysPrefix = "[SYS]";
    const string actPrefix = "[ACT]";
    const string loadPrefix = "LOAD_STATE:";


    std::string currentScenario = "NONE";
    std::string currentState = "NONE";
    std::string currentStatus = "NOT RUNNING";

    bool isPaused = false;

protected:

    /// Event listener for Logs.
    void onNewLog(AMM::Log &log, SampleInfo_t *info);

    /// Event listener for Module Configuration
    void onNewModuleConfiguration(AMM::ModuleConfiguration &mc, SampleInfo_t *info);

    /// Event listener for Status
    void onNewStatus(AMM::Status &status, SampleInfo_t *info);

    /// Event listener for Simulation Controller
    void onNewSimulationControl(AMM::SimulationControl &simControl, SampleInfo_t *info);

    /// Event listener for Assessment.
    void onNewAssessment(AMM::Assessment &assessment, SampleInfo_t *info);

    /// Event listener for Event Fragment.
    void onNewEventFragment(AMM::EventFragment &eventFrag, SampleInfo_t *info);

    /// Event listener for Event Record.
    void onNewEventRecord(AMM::EventRecord &eventRec, SampleInfo_t *info);

    /// Event listener for Fragment Amendment Request.
    void onNewFragmentAmendmentRequest(AMM::FragmentAmendmentRequest &ffar, SampleInfo_t *info);

    /// Event listener for Omitted Event.
    void onNewOmittedEvent(AMM::OmittedEvent &omittedEvent, SampleInfo_t *info);

    /// Event listener for Operational Description.
    void onNewOperationalDescription(AMM::OperationalDescription &opDescript, SampleInfo_t *info);

    /// Event listener for Render Modification.
    void onNewRenderModification(AMM::RenderModification &rendMod, SampleInfo_t *info);

    /// Event listener for Physiology Modification.
    void onNewPhysiologyModification(AMM::PhysiologyModification &physMod, SampleInfo_t *info);

    /// Event lsitener for Command.
    void onNewCommand(AMM::Command &command, eprosima::fastrtps::SampleInfo_t *info);

    void onNewPhysiologyWaveform(AMM::PhysiologyWaveform &n, SampleInfo_t *info);

    void onNewPhysiologyValue(AMM::PhysiologyValue &n, SampleInfo_t *info);


};

