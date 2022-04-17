
#include "Manikin.h"

using namespace std;
using namespace std::chrono;
using namespace AMM;
using namespace tinyxml2;


Manikin::Manikin(std::string mid) {
    manikin_id = mid;

    LOG_INFO << "Initializing manikin manager and listener for " << mid;
    mgr = new DDSManager<Manikin>(config_file, manikin_id);

    mgr->InitializeCommand();
    mgr->InitializeInstrumentData();
    mgr->InitializeSimulationControl();
    mgr->InitializePhysiologyModification();
    mgr->InitializeRenderModification();
    mgr->InitializeAssessment();
    mgr->InitializePhysiologyValue();
    mgr->InitializePhysiologyWaveform();
    mgr->InitializeEventRecord();
    mgr->InitializeOperationalDescription();
    mgr->InitializeModuleConfiguration();
    mgr->InitializeStatus();
    mgr->InitializeOmittedEvent();

    mgr->CreateOperationalDescriptionPublisher();
    mgr->CreateModuleConfigurationPublisher();
    mgr->CreateStatusPublisher();
    mgr->CreateEventRecordPublisher();
    mgr->CreatePhysiologyValueSubscriber(this, &Manikin::onNewPhysiologyValue);
    mgr->CreatePhysiologyWaveformSubscriber(this, &Manikin::onNewPhysiologyWaveform);
    mgr->CreateCommandSubscriber(this, &Manikin::onNewCommand);
    mgr->CreateSimulationControlSubscriber(this, &Manikin::onNewSimulationControl);
    mgr->CreateAssessmentSubscriber(this, &Manikin::onNewAssessment);
    mgr->CreateRenderModificationSubscriber(this, &Manikin::onNewRenderModification);
    mgr->CreatePhysiologyModificationSubscriber(this, &Manikin::onNewPhysiologyModification);
    mgr->CreateEventRecordSubscriber(this, &Manikin::onNewEventRecord);
    mgr->CreateOmittedEventSubscriber(this, &Manikin::onNewOmittedEvent);
    mgr->CreateOperationalDescriptionSubscriber(this, &Manikin::onNewOperationalDescription);
    mgr->CreateModuleConfigurationSubscriber(this, &Manikin::onNewModuleConfiguration);
    mgr->CreateRenderModificationPublisher();
    mgr->CreatePhysiologyModificationPublisher();
    mgr->CreateSimulationControlPublisher();
    mgr->CreateCommandPublisher();
    mgr->CreateInstrumentDataPublisher();
    mgr->CreateAssessmentPublisher();
    m_uuid.id(mgr->GenerateUuidString());

    std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

void Manikin::SetServer(Server *srv) {
    s = srv;

}

Manikin::~Manikin() {
    mgr->Shutdown();
}

void Manikin::onNewModuleConfiguration(AMM::ModuleConfiguration &mc, SampleInfo_t *info) {
    LOG_INFO << "** Message came in on manikin " << manikin_id;
    LOG_TRACE << "Module Configuration recieved for " << mc.name();

    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        std::string cid = it->first;
        std::string clientType;
        auto pos = clientTypeMap.find(it->first);
        if (pos == clientTypeMap.end()) {
            //handle the error
        } else {
            clientType = pos->second;
        }
        if (clientType.find(mc.name()) != std::string::npos) {
            Client *c = Server::GetClientByIndex(cid);
            if (c) {
                std::string encodedConfigContent = Utility::encode64(mc.capabilities_configuration());
                std::ostringstream encodedConfig;
                encodedConfig << configPrefix << encodedConfigContent << ";mid=" << manikin_id << std::endl;
                Server::SendToClient(c, encodedConfig.str());
            }
        }
        ++it;
    }
}

/// Event handler for incoming Physiology Waveform data.
void Manikin::onNewPhysiologyWaveform(AMM::PhysiologyWaveform &n, SampleInfo_t *info) {
    std::string hfname = "HF_" + n.name();
    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        std::string cid = it->first;
        std::vector <std::string> subV = subscribedTopics[cid];
        if (std::find(subV.begin(), subV.end(), hfname) != subV.end()) {
            Client *c = Server::GetClientByIndex(cid);
            if (c) {
                std::ostringstream messageOut;
                messageOut << n.name() << "=" << n.value() << ";mid=" << manikin_id << "|" << std::endl;
                string stringOut = messageOut.str();
                Server::SendToClient(c, messageOut.str());
            }
        }
        ++it;
    }
}

void Manikin::onNewPhysiologyValue(AMM::PhysiologyValue &n, SampleInfo_t *info) {
    // Drop values into the lab sheets
    for (auto &outer_map_pair : labNodes) {
        if (labNodes[outer_map_pair.first].find(n.name()) !=
            labNodes[outer_map_pair.first].end()) {
            labNodes[outer_map_pair.first][n.name()] = n.value();
        }
    }

    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        std::string cid = it->first;
        std::vector <std::string> subV = subscribedTopics[cid];

        if (std::find(subV.begin(), subV.end(), n.name()) != subV.end()) {
            Client *c = Server::GetClientByIndex(cid);
            if (c) {
                std::ostringstream messageOut;
                messageOut << n.name() << "=" << n.value() << ";mid=" << manikin_id << "|" << std::endl;
                Server::SendToClient(c, messageOut.str());
            }
        }
        ++it;
    }
}

void Manikin::onNewPhysiologyModification(AMM::PhysiologyModification &pm, SampleInfo_t *info) {
    LOG_INFO << "** Message came in on manikin " << manikin_id;
    std::string location;
    std::string practitioner;

    if (eventRecords.count(pm.event_id().id()) > 0) {
        AMM::EventRecord er = eventRecords[pm.event_id().id()];
        location = er.location().name();
        practitioner = er.agent_id().id();
    }

    std::ostringstream messageOut;
    messageOut << "[AMM_Physiology_Modification]"
               << "id=" << pm.id().id() << ";"
               << "mid=" << manikin_id << ";"
               << "event_id=" << pm.event_id().id() << ";"
               << "type=" << pm.type() << ";"
               << "location=" << location << ";"
               << "participant_id=" << practitioner << ";"
               << "payload=" << pm.data()
               << std::endl;
    string stringOut = messageOut.str();

    LOG_DEBUG << "Received a phys mod via DDS, republishing to TCP clients: " << stringOut;

    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        std::string cid = it->first;
        std::vector <std::string> subV = subscribedTopics[cid];

        if (std::find(subV.begin(), subV.end(), pm.type()) != subV.end() ||
            std::find(subV.begin(), subV.end(), "AMM_Physiology_Modification") !=
            subV.end()) {
            Client *c = Server::GetClientByIndex(cid);
            if (c) {
                Server::SendToClient(c, stringOut);
            }
        }
        ++it;
    }
}

void Manikin::onNewOmittedEvent(AMM::OmittedEvent &oe, SampleInfo_t *info) {
    std::string location;
    std::string practitioner;
    std::string eType;
    std::string eData;
    std::string pType;

    // Make a mock event record
    AMM::EventRecord er;
    er.id(oe.id());
    er.location(oe.location());
    er.agent_id(oe.agent_id());
    er.type(oe.type());
    er.timestamp(oe.timestamp());
    er.agent_type(oe.agent_type());
    er.data(oe.data());

    LOG_DEBUG << "Received an omitted event record of type " << er.type()
              << " on DDS bus, so we're storing it in a simple map.";
    eventRecords[er.id().id()] = er;
    location = er.location().name();
    practitioner = er.agent_id().id();
    eType = er.type();
    eData = er.data();
    pType = AMM::Utility::EEventAgentTypeStr(er.agent_type());

    std::ostringstream messageOut;

    messageOut << "[AMM_OmittedEvent]"
               << "id=" << er.id().id() << ";"
               << "mid=" << manikin_id << ";"
               << "type=" << eType << ";"
               << "location=" << location << ";"
               << "participant_id=" << practitioner << ";"
               << "participant_type=" << pType << ";"
               << "data=" << eData << ";"
               << std::endl;
    string stringOut = messageOut.str();

    LOG_DEBUG << "Received an EventRecord via DDS, republishing to TCP clients: " << stringOut;

    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        std::string cid = it->first;
        std::vector <std::string> subV = subscribedTopics[cid];
        if (std::find(subV.begin(), subV.end(), "AMM_EventRecord") != subV.end()) {
            Client *c = Server::GetClientByIndex(cid);
            if (c) {
                Server::SendToClient(c, stringOut);
            }
        }
        ++it;
    }
}

void Manikin::onNewEventRecord(AMM::EventRecord &er, SampleInfo_t *info) {
    std::string location;
    std::string practitioner;
    std::string eType;
    std::string eData;
    std::string pType;

    LOG_DEBUG << "Received an event record of type " << er.type()
              << " on DDS bus, so we're storing it in a simple map.";
    eventRecords[er.id().id()] = er;
    location = er.location().name();
    practitioner = er.agent_id().id();
    eType = er.type();
    eData = er.data();
    pType = AMM::Utility::EEventAgentTypeStr(er.agent_type());

    std::ostringstream messageOut;

    messageOut << "[AMM_EventRecord]"
               << "id=" << er.id().id() << ";"
               << "mid=" << manikin_id << ";"
               << "type=" << eType << ";"
               << "location=" << location << ";"
               << "participant_id=" << practitioner << ";"
               << "participant_type=" << pType << ";"
               << "data=" << eData << ";"
               << std::endl;
    string stringOut = messageOut.str();

    LOG_DEBUG << "Received an EventRecord via DDS, republishing to TCP clients: " << stringOut;

    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        std::string cid = it->first;
        std::vector <std::string> subV = subscribedTopics[cid];
        if (std::find(subV.begin(), subV.end(), "AMM_EventRecord") != subV.end()) {
            Client *c = Server::GetClientByIndex(cid);
            if (c) {
                Server::SendToClient(c, stringOut);
            }
        }
        ++it;
    }
}

void Manikin::onNewAssessment(AMM::Assessment &a, eprosima::fastrtps::SampleInfo_t *info) {
    std::string location;
    std::string practitioner;
    std::string eType;

    LOG_INFO << "Assessment received on DDS bus";
    if (eventRecords.count(a.event_id().id()) > 0) {
        AMM::EventRecord er = eventRecords[a.event_id().id()];
        location = er.location().name();
        practitioner = er.agent_id().id();
        eType = er.type();
    }

    std::ostringstream messageOut;

    messageOut << "[AMM_Assessment]"
               << "id=" << a.id().id() << ";"
               << "mid=" << manikin_id << ";"
               << "event_id=" << a.event_id().id() << ";"
               << "type=" << eType << ";"
               << "location=" << location << ";"
               << "participant_id=" << practitioner << ";"
               << "value=" << AMM::Utility::EAssessmentValueStr(a.value()) << ";"
               << "comment=" << a.comment()
               << std::endl;
    string stringOut = messageOut.str();

    LOG_DEBUG << "Received an assessment via DDS, republishing to TCP clients: " << stringOut;

    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        std::string cid = it->first;
        std::vector <std::string> subV = subscribedTopics[cid];
        if (std::find(subV.begin(), subV.end(), "AMM_Assessment") != subV.end()) {
            Client *c = Server::GetClientByIndex(cid);
            if (c) {
                Server::SendToClient(c, stringOut);
            }
        }
        ++it;
    }
}

void Manikin::onNewRenderModification(AMM::RenderModification &rendMod, SampleInfo_t *info) {
    LOG_INFO << "** Message came in on manikin " << manikin_id;
    std::string location;
    std::string practitioner;

    // LOG_INFO << "Render mod received on DDS bus";
    if (eventRecords.count(rendMod.event_id().id()) > 0) {
        AMM::EventRecord er = eventRecords[rendMod.event_id().id()];
        location = er.location().name();
        practitioner = er.agent_id().id();
    }

    std::ostringstream messageOut;
    std::string rendModPayload;
    std::string rendModType;
    if (rendMod.data().empty()) {
        rendModPayload = "<RenderModification type='" + rendMod.type() + "'/>";
        rendModType = "";
    } else {
        rendModPayload = rendMod.data();
        rendModType = rendMod.type();
    }
    messageOut << "[AMM_Render_Modification]"
               << "id=" << rendMod.id().id() << ";"
               << "mid=" << manikin_id << ";"
               << "event_id=" << rendMod.event_id().id() << ";"
               << "type=" << rendModType << ";"
               << "location=" << location << ";"
               << "participant_id=" << practitioner << ";"
               // << "payload=" << rendMod.data()
               << "payload=" << rendModPayload
               << std::endl;
    string stringOut = messageOut.str();

    if (rendModPayload.find("START_OF") == std::string::npos) {
        LOG_DEBUG << "Received a render mod via DDS, republishing to TCP clients: " << stringOut;
    }

    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        std::string cid = it->first;
        std::vector <std::string> subV = subscribedTopics[cid];
        if (std::find(subV.begin(), subV.end(), rendMod.type()) != subV.end() ||
            std::find(subV.begin(), subV.end(), "AMM_Render_Modification") !=
            subV.end()) {
            Client *c = Server::GetClientByIndex(cid);
            if (c) {
                Server::SendToClient(c, stringOut);
            }
        }
        ++it;
    }
}

void Manikin::onNewSimulationControl(AMM::SimulationControl &simControl, SampleInfo_t *info) {
    bool doWriteTopic = false;

    switch (simControl.type()) {
        case AMM::ControlType::RUN: {
            currentStatus = "RUNNING";
            isPaused = false;
            LOG_INFO << "Message recieved; Run sim.";
            std::ostringstream tmsg;
            tmsg << "ACT=START_SIM" << ";mid=" << manikin_id << std::endl;
            s->SendToAll(tmsg.str());
            break;
        }

        case AMM::ControlType::HALT: {
            if (isPaused) {
                currentStatus = "PAUSED";
            } else {
                currentStatus = "NOT RUNNING";
            }
            LOG_INFO << "Message recieved; Halt sim";
            std::ostringstream tmsg;
            tmsg << "ACT=PAUSE_SIM" << ";mid=" << manikin_id << std::endl;
            s->SendToAll(tmsg.str());
            break;
        }

        case AMM::ControlType::RESET: {
            currentStatus = "NOT RUNNING";
            isPaused = false;
            LOG_INFO << "Message recieved; Reset sim";
            std::ostringstream tmsg;
            tmsg << "ACT=RESET_SIM" << ";mid=" << manikin_id << std::endl;
            s->SendToAll(tmsg.str());
            break;
        }

        case AMM::ControlType::SAVE: {
            LOG_INFO << "Message recieved; Save sim";
            //SaveSimulation(doWriteTopic);
            break;
        }
    }
}

void Manikin::onNewOperationalDescription(AMM::OperationalDescription &opD, SampleInfo_t *info) {
    LOG_INFO << "** Message came in on manikin " << manikin_id;
    LOG_INFO << "Operational description for module " << opD.name() << " / model " << opD.model();

    // [AMM_OperationalDescription]name=;description=;manufacturer=;model=;serial_number=;module_id=;module_version=;configuration_version=;AMM_version=;capabilities_configuration=(BASE64 ENCODED STRING - URLSAFE)

    std::ostringstream messageOut;
    std::string capabilities = Utility::encode64(opD.capabilities_schema());

    messageOut << "[AMM_OperationalDescription]"
               << "name=" << opD.name() << ";"
               << "mid=" << manikin_id << ";"
               << "description=" << opD.description() << ";"
               << "manufacturer=" << opD.manufacturer() << ";"
               << "model=" << opD.model() << ";"
               << "serial_number=" << opD.serial_number() << ";"
               << "module_id=" << opD.module_id().id() << ";"
               << "module_version=" << opD.module_version() << ";"
               << "configuration_version=" << opD.configuration_version() << ";"
               << "AMM_version=" << opD.AMM_version() << ";"
               << "capabilities_configuration=" << capabilities
               << std::endl;
    string stringOut = messageOut.str();

    LOG_DEBUG << "Received an Operational Description via DDS, republishing to TCP clients: " << stringOut;

    auto it = clientMap.begin();
    while (it != clientMap.end()) {
        std::string cid = it->first;
        std::vector <std::string> subV = subscribedTopics[cid];
        if (std::find(subV.begin(), subV.end(), "AMM_OperationalDescription") != subV.end()) {
            Client *c = Server::GetClientByIndex(cid);
            if (c) {
                Server::SendToClient(c, stringOut);
            }
        }
        ++it;
    }
}

void Manikin::onNewCommand(AMM::Command &c, eprosima::fastrtps::SampleInfo_t *info) {
    LOG_INFO << "** Message came in on manikin " << manikin_id << ": " << c.message();
    if (!c.message().compare(0, sysPrefix.size(), sysPrefix)) {
        std::string value = c.message().substr(sysPrefix.size());
        if (value.find("START_SIM") != std::string::npos) {
            currentStatus = "RUNNING";
            isPaused = false;
            AMM::SimulationControl simControl;
            auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            simControl.timestamp(ms);
            simControl.type(AMM::ControlType::RUN);
            mgr->WriteSimulationControl(simControl);
            std::string tmsg = "ACT=START_SIM;mid=" + manikin_id;
            s->SendToAll(tmsg);
        } else if (value.find("STOP_SIM") != std::string::npos) {
            currentStatus = "NOT RUNNING";
            isPaused = false;
            AMM::SimulationControl simControl;
            auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            simControl.timestamp(ms);
            simControl.type(AMM::ControlType::HALT);
            mgr->WriteSimulationControl(simControl);
            std::string tmsg = "ACT=STOP_SIM;mid=" + manikin_id;
            s->SendToAll(tmsg);
        } else if (value.find("PAUSE_SIM") != std::string::npos) {
            currentStatus = "PAUSED";
            isPaused = true;
            AMM::SimulationControl simControl;
            auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            simControl.timestamp(ms);
            simControl.type(AMM::ControlType::HALT);
            mgr->WriteSimulationControl(simControl);
            std::string tmsg = "ACT=PAUSE_SIM;mid=" + manikin_id;
            s->SendToAll(tmsg);
        } else if (value.find("RESET_SIM") != std::string::npos) {
            currentStatus = "NOT RUNNING";
            isPaused = false;
            std::string tmsg = "ACT=RESET_SIM;mid=" + manikin_id;
            s->SendToAll(tmsg);
            AMM::SimulationControl simControl;
            auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            simControl.timestamp(ms);
            simControl.type(AMM::ControlType::RESET);
            mgr->WriteSimulationControl(simControl);
            InitializeLabNodes();
        } else if (value.find("END_SIMULATION") != std::string::npos) {
            currentStatus = "NOT RUNNING";
            isPaused = true;
            AMM::SimulationControl simControl;
            auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
            simControl.timestamp(ms);
            simControl.type(AMM::ControlType::HALT);
            mgr->WriteSimulationControl(simControl);
            std::string tmsg = "ACT=END_SIMULATION_SIM;mid=" + manikin_id;
            s->SendToAll(tmsg);
        } else if (!value.compare(0, loadScenarioPrefix.size(), loadScenarioPrefix)) {
            // currentScenario = value.substr(loadScenarioPrefix.size());
            // sendConfigToAll(currentScenario);
            // std::ostringstream messageOut;
            // messageOut << "ACT" << "=" << c.message() << std::endl;
            // s->SendToAll(messageOut.str());
        } else if (!value.compare(0, loadPrefix.size(), loadPrefix)) {
            // currentState = value.substr(loadStatePrefix.size());
            // std::ostringstream messageOut;
            // messageOut << "ACT" << "=" << c.message() << std::endl;
            // s->SendToAll(messageOut.str());
        } else {
            std::ostringstream messageOut;
            messageOut << "ACT" << "=" << c.message() << ";mid=" << manikin_id << std::endl;
            LOG_INFO << "Sending unknown system message: " << messageOut.str();
            s->SendToAll(messageOut.str());
        }
    } else {
        std::ostringstream messageOut;
        messageOut << "ACT"
                   << "=" << c.message() << ";mid=" << manikin_id << std::endl;
        LOG_INFO << "Sending unknown message: " << messageOut.str();
        s->SendToAll(messageOut.str());
    }
}

void Manikin::PublishSettings(std::string const &equipmentType) {
    std::ostringstream payload;
    LOG_INFO << "Publishing equipment " << equipmentType << " settings";
    for (auto &inner_map_pair : equipmentSettings[equipmentType]) {
        payload << inner_map_pair.first << "=" << inner_map_pair.second
                << std::endl;
        LOG_DEBUG << "\t" << inner_map_pair.first << ": " << inner_map_pair.second;
    }

    AMM::InstrumentData i;
    i.instrument(equipmentType);
    i.payload(payload.str());
    mgr->WriteInstrumentData(i);
}

void Manikin::HandleSettings(Client *c, std::string const &settingsVal) {
    XMLDocument doc(false);
    doc.Parse(settingsVal.c_str());
    tinyxml2::XMLNode *root =
            doc.FirstChildElement("AMMModuleConfiguration");
    tinyxml2::XMLElement *module = root->FirstChildElement("module");
    tinyxml2::XMLElement *caps =
            module->FirstChildElement("capabilities");
    if (caps) {
        for (tinyxml2::XMLNode *node =
                caps->FirstChildElement("capability");
             node; node = node->NextSibling()) {
            tinyxml2::XMLElement *cap = node->ToElement();
            std::string capabilityName = cap->Attribute("name");
            tinyxml2::XMLElement *configEl =
                    cap->FirstChildElement("configuration");
            if (configEl) {
                for (tinyxml2::XMLNode *settingNode =
                        configEl->FirstChildElement("setting");
                     settingNode; settingNode = settingNode->NextSibling()) {
                    tinyxml2::XMLElement *setting = settingNode->ToElement();
                    std::string settingName = setting->Attribute("name");
                    std::string settingValue = setting->Attribute("value");
                    equipmentSettings[capabilityName][settingName] =
                            settingValue;
                }
            }
            PublishSettings(capabilityName);
        }
    }
}

void Manikin::HandleCapabilities(Client *c, std::string const &capabilityVal) {
    XMLDocument doc(false);
    doc.Parse(capabilityVal.c_str());
    tinyxml2::XMLNode *root = doc.FirstChildElement("AMMModuleConfiguration");
    tinyxml2::XMLElement *module = root->FirstChildElement("module")->ToElement();
    const char *name = module->Attribute("name");
    const char *manufacturer = module->Attribute("manufacturer");
    const char *model = module->Attribute("model");
    const char *serial = module->Attribute("serial_number");
    const char *module_version = module->Attribute("module_version");

    std::string nodeName(name);
    std::string nodeManufacturer(manufacturer);
    std::string nodeModel(model);
    std::string serialNumber(serial);
    std::string moduleVersion(module_version);

    AMM::OperationalDescription od;
    od.name(nodeName);
    od.model(nodeModel);
    od.manufacturer(nodeManufacturer);
    od.serial_number(serialNumber);
    // od.module_id(m_uuid);
    od.module_version(moduleVersion);
    od.capabilities_schema(capabilityVal);
    od.description();

    // mgr->WriteOperationalDescription(od);

    LOG_INFO << "Making client entry";
    // Set the client's type
    ServerThread::LockMutex(c->id);
    c->SetClientType(nodeName);
    try {
        clientTypeMap.insert({c->id, nodeName});
    } catch (exception &e) {
        LOG_ERROR << "Unable to insert into clientTypeMap: " << e.what();
    }
    ServerThread::UnlockMutex(c->id);

    LOG_INFO << "Clearing topic trackers";
    subscribedTopics[c->id].clear();
    publishedTopics[c->id].clear();

    tinyxml2::XMLElement *caps =
            module->FirstChildElement("capabilities");
    if (caps) {
        for (tinyxml2::XMLNode *node = caps->FirstChildElement("capability"); node; node = node->NextSibling()) {
            tinyxml2::XMLElement *cap = node->ToElement();
            std::string capabilityName = cap->Attribute("name");
            tinyxml2::XMLElement *starting_settings =
                    cap->FirstChildElement("starting_settings");
            if (starting_settings) {
                for (tinyxml2::XMLNode *settingNode =
                        starting_settings->FirstChildElement("setting");
                     settingNode; settingNode = settingNode->NextSibling()) {
                    tinyxml2::XMLElement *setting = settingNode->ToElement();
                    std::string settingName = setting->Attribute("name");
                    std::string settingValue = setting->Attribute("value");
                    equipmentSettings[capabilityName][settingName] =
                            settingValue;
                }
                PublishSettings(capabilityName);
            }

            tinyxml2::XMLNode *subs =
                    node->FirstChildElement("subscribed_topics");
            if (subs) {
                for (tinyxml2::XMLNode *sub =
                        subs->FirstChildElement("topic");
                     sub; sub = sub->NextSibling()) {
                    tinyxml2::XMLElement *s = sub->ToElement();
                    std::string subTopicName = s->Attribute("name");

                    if (s->Attribute("nodepath")) {
                        std::string subNodePath = s->Attribute("nodepath");
                        if (subTopicName == "AMM_HighFrequencyNode_Data") {
                            subTopicName = "HF_" + subNodePath;
                        } else {
                            subTopicName = subNodePath;
                        }
                    }
                    Utility::add_once(subscribedTopics[c->id], subTopicName);
                    LOG_DEBUG << "[" << capabilityName << "][" << c->id
                              << "] Subscribing to " << subTopicName;
                }
            }

            // Store published topics for this capability
            tinyxml2::XMLNode *pubs =
                    node->FirstChildElement("published_topics");
            if (pubs) {
                for (tinyxml2::XMLNode *pub =
                        pubs->FirstChildElement("topic");
                     pub; pub = pub->NextSibling()) {
                    tinyxml2::XMLElement *p = pub->ToElement();
                    std::string pubTopicName = p->Attribute("name");
                    Utility::add_once(publishedTopics[c->id], pubTopicName);
                    LOG_DEBUG << "[" << capabilityName << "][" << c->id
                              << "] Publishing " << pubTopicName;
                }
            }
        }
    }
}

void Manikin::HandleStatus(Client *c, std::string const &statusVal) {
    XMLDocument doc(false);
    doc.Parse(statusVal.c_str());

    tinyxml2::XMLNode *root = doc.FirstChildElement("AMMModuleStatus");
    tinyxml2::XMLElement *module =
            root->FirstChildElement("module")->ToElement();
    const char *name = module->Attribute("name");
    std::string nodeName(name);

    std::size_t found = statusVal.find(haltingString);
    AMM::Status s;
    s.module_id(m_uuid);
    s.capability(nodeName);
    if (found != std::string::npos) {
        s.value(AMM::StatusValue::INOPERATIVE);
    } else {
        s.value(AMM::StatusValue::OPERATIONAL);
    }
    mgr->WriteStatus(s);
}

void Manikin::DispatchRequest(Client *c, std::string const &request, std::string mid) {
    if (boost::starts_with(request, "STATUS")) {
        LOG_DEBUG << "STATUS request";
        std::ostringstream messageOut;
        messageOut << "STATUS" << "=" << currentStatus << "|";
        messageOut << "SCENARIO" << "=" << currentScenario << "|";
        messageOut << "STATE" << "=" << currentState << "|";
        Server::SendToClient(c, messageOut.str());
    } else if (boost::starts_with(request, "LABS")) {
        LOG_DEBUG << "LABS request: " << request;
        const auto equals_idx = request.find_first_of(';');
        if (std::string::npos != equals_idx) {
            auto str = request.substr(equals_idx + 1);
            LOG_DEBUG << "Return lab values for " << str;
            auto it = labNodes[str].begin();
            while (it != labNodes[str].end()) {
                std::ostringstream messageOut;
                messageOut << it->first << "=" << it->second << ":" << str << ";mid=" << mid << "|";
                Server::SendToClient(c, messageOut.str());
                ++it;
            }
        } else {
            LOG_DEBUG << "No specific labs requested, return all values.";
            auto it = labNodes["ALL"].begin();
            while (it != labNodes["ALL"].end()) {
                std::ostringstream messageOut;
                messageOut << it->first << "=" << it->second << ";mid=" << mid << "|";
                Server::SendToClient(c, messageOut.str());
                ++it;
            }
        }
    }
}

void Manikin::PublishOperationalDescription() {
    AMM::OperationalDescription od;
    od.name(moduleName);
    od.model("TCP Bridge");
    od.manufacturer("Vcom3D");
    od.serial_number("1.0.0");
    od.module_id(m_uuid);
    od.module_version("1.0.0");
    const std::string capabilities = AMM::Utility::read_file_to_string("config/tcp_bridge_capabilities.xml");
    od.capabilities_schema(capabilities);
    od.description();
    mgr->WriteOperationalDescription(od);
}

void Manikin::PublishConfiguration() {
    AMM::ModuleConfiguration mc;
    auto ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    mc.timestamp(ms);
    mc.module_id(m_uuid);
    mc.name(moduleName);
    const std::string configuration = AMM::Utility::read_file_to_string("config/tcp_bridge_configuration.xml");
    mc.capabilities_configuration(configuration);
    mgr->WriteModuleConfiguration(mc);
}

void Manikin::InitializeLabNodes() {
    //
    labNodes["ALL"]["Substance_Sodium"] = 0.0f;
    labNodes["ALL"]["MetabolicPanel_CarbonDioxide"] = 0.0f;
    labNodes["ALL"]["Substance_Glucose_Concentration"] = 0.0f;
    labNodes["ALL"]["BloodChemistry_BloodUreaNitrogen_Concentration"] = 0.0f;
    labNodes["ALL"]["Substance_Creatinine_Concentration"] = 0.0f;
    labNodes["ALL"]["BloodChemistry_WhiteBloodCell_Count"] = 0.0f;
    labNodes["ALL"]["BloodChemistry_RedBloodCell_Count"] = 0.0f;
    labNodes["ALL"]["Substance_Hemoglobin_Concentration"] = 0.0f;
    labNodes["ALL"]["BloodChemistry_Hemaocrit"] = 0.0f;
    labNodes["ALL"]["CompleteBloodCount_Platelet"] = 0.0f;
    labNodes["ALL"]["BloodChemistry_BloodPH"] = 0.0f;
    labNodes["ALL"]["BloodChemistry_Arterial_CarbonDioxide_Pressure"] = 0.0f;
    labNodes["ALL"]["BloodChemistry_Arterial_Oxygen_Pressure"] = 0.0f;
    labNodes["ALL"]["Substance_Bicarbonate"] = 0.0f;
    labNodes["ALL"]["Substance_BaseExcess"] = 0.0f;
    labNodes["ALL"]["Substance_Lactate_Concentration_mmol"] = 0.0f;
    labNodes["ALL"]["BloodChemistry_CarbonMonoxide_Saturation"] = 0.0f;
    labNodes["ALL"]["Anion_Gap"] = 0.0f;
    labNodes["ALL"]["Substance_Ionized_Calcium"] = 0.0f;

    labNodes["POCT"]["Substance_Sodium"] = 0.0f;
    labNodes["POCT"]["MetabolicPanel_Potassium"] = 0.0f;
    labNodes["POCT"]["MetabolicPanel_Chloride"] = 0.0f;
    labNodes["POCT"]["MetabolicPanel_CarbonDioxide"] = 0.0f;
    labNodes["POCT"]["Substance_Glucose_Concentration"] = 0.0f;
    labNodes["POCT"]["BloodChemistry_BloodUreaNitrogen_Concentration"] = 0.0f;
    labNodes["POCT"]["Substance_Creatinine_Concentration"] = 0.0f;
    labNodes["POCT"]["Anion_Gap"] = 0.0f;
    labNodes["POCT"]["Substance_Ionized_Calcium"] = 0.0f;

    labNodes["Hematology"]["BloodChemistry_Hemaocrit"] = 0.0f;
    labNodes["Hematology"]["Substance_Hemoglobin_Concentration"] = 0.0f;

    labNodes["ABG"]["BloodChemistry_BloodPH"] = 0.0f;
    labNodes["ABG"]["BloodChemistry_Arterial_CarbonDioxide_Pressure"] = 0.0f;
    labNodes["ABG"]["BloodChemistry_Arterial_Oxygen_Pressure"] = 0.0f;
    labNodes["ABG"]["MetabolicPanel_CarbonDioxide"] = 0.0f;
    labNodes["ABG"]["Substance_Bicarbonate"] = 0.0f;
    labNodes["ABG"]["Substance_BaseExcess"] = 0.0f;
    labNodes["ABG"]["BloodChemistry_Oxygen_Saturation"] = 0.0f;
    labNodes["ABG"]["Substance_Lactate_Concentration_mmol"] = 0.0f;
    labNodes["ABG"]["BloodChemistry_CarbonMonoxide_Saturation"] = 0.0f;

    labNodes["VBG"]["BloodChemistry_BloodPH"] = 0.0f;
    labNodes["VBG"]["BloodChemistry_Arterial_CarbonDioxide_Pressure"] = 0.0f;
    labNodes["VBG"]["BloodChemistry_Arterial_Oxygen_Pressure"] = 0.0f;
    labNodes["VBG"]["MetabolicPanel_CarbonDioxide"] = 0.0f;
    labNodes["VBG"]["Substance_Bicarbonate"] = 0.0f;
    labNodes["VBG"]["Substance_BaseExcess"] = 0.0f;
    labNodes["VBG"]["BloodChemistry_VenousCarbonDioxidePressure"] = 0.0f;
    labNodes["VBG"]["BloodChemistry_VenousOxygenPressure"] = 0.0f;
    labNodes["VBG"]["Substance_Lactate_Concentration_mmol"] = 0.0f;
    labNodes["VBG"]["BloodChemistry_CarbonMonoxide_Saturation"] = 0.0f;

    labNodes["BMP"]["Substance_Sodium"] = 0.0f;
    labNodes["BMP"]["MetabolicPanel_Potassium"] = 0.0f;
    labNodes["BMP"]["MetabolicPanel_Chloride"] = 0.0f;
    labNodes["BMP"]["MetabolicPanel_CarbonDioxide"] = 0.0f;
    labNodes["BMP"]["Substance_Glucose_Concentration"] = 0.0f;
    labNodes["BMP"]["BloodChemistry_BloodUreaNitrogen_Concentration"] = 0.0f;
    labNodes["BMP"]["Substance_Creatinine_Concentration"] = 0.0f;
    labNodes["BMP"]["Anion_Gap"] = 0.0f;
    labNodes["BMP"]["Substance_Ionized_Calcium"] = 0.0f;

    labNodes["CBC"]["BloodChemistry_WhiteBloodCell_Count"] = 0.0f;
    labNodes["CBC"]["BloodChemistry_RedBloodCell_Count"] = 0.0f;
    labNodes["CBC"]["Substance_Hemoglobin_Concentration"] = 0.0f;
    labNodes["CBC"]["BloodChemistry_Hemaocrit"] = 0.0f;
    labNodes["CBC"]["CompleteBloodCount_Platelet"] = 0.0f;

    labNodes["CMP"]["Substance_Albumin_Concentration"] = 0.0f;
    labNodes["CMP"]["BloodChemistry_BloodUreaNitrogen_Concentration"] = 0.0f;
    labNodes["CMP"]["Substance_Calcium_Concentration"] = 0.0f;
    labNodes["CMP"]["MetabolicPanel_Chloride"] = 0.0f;
    labNodes["CMP"]["MetabolicPanel_CarbonDioxide"] = 0.0f;
    labNodes["CMP"]["Substance_Creatinine_Concentration"] = 0.0f;
    labNodes["CMP"]["Substance_Glucose_Concentration"] = 0.0f;
    labNodes["CMP"]["MetabolicPanel_Potassium"] = 0.0f;
    labNodes["CMP"]["Substance_Sodium"] = 0.0f;
    labNodes["CMP"]["MetabolicPanel_Bilirubin"] = 0.0f;
    labNodes["CMP"]["MetabolicPanel_Protein"] = 0.0f;
}