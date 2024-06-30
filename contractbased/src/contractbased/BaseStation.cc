#include <omnetpp.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>

#include "veins/base/modules/BaseApplLayer.h"
#include "veins/base/modules/BaseMacLayer.h"
#include "message_m.h"

using namespace std;
using namespace omnetpp;
using namespace veins;

struct Vehicle {
    double taskResource;
    double taskDataSize;
    double delayConstraint;
    double taskPrice;
    double sharedResource;
    double price;
    bool isTaskReady = false;
    bool isTaskAssigned = false;
    double *totalTime;
    int taskAssignedFrom;

    Coord position;
    Coord speed;

    int address;
};

static size_t writeCallback(void *contents, size_t size, size_t nmemb, std::string *s) {
    size_t newLength = size * nmemb;
    try {
        s->append((char *) contents, newLength);
    } catch (std::bad_alloc &e) {
        // handle memory problem
        return 0;
    }
    return newLength;
}

class BaseStation : public BaseApplLayer {
private:
    // Contrct
    double unitBenefit;
    double computationCapability;
    int duration;
    vector<double> typeProbability;
    int totalVehicles;
    double deltaMin;
    double deltaMax;

    // Task Scheduler
    int taskAssignmentThreshold;
    int numVehicles;
    Vehicle *vehicles;

    int lastVehicleId;
    map<int, int> vehicleIdMap;

    ContractList *contractList;

    int baseStationTasks;

protected:
    virtual void initialize(int stage) override {
        BaseApplLayer::initialize(stage);
        // Read parameters from .ned file
        unitBenefit = par("unitBenefit");
        computationCapability = par("computationCapability");
        duration = par("duration");
        totalVehicles = par("totalVehicles");
        deltaMin = par("deltaMin");
        deltaMax = par("deltaMax");
        baseStationTasks = 0;

        taskAssignmentThreshold = par("taskAssignmentThreshold");

        numVehicles = totalVehicles;
        vehicles = new Vehicle[numVehicles];

        lastVehicleId = 0;

        if (stage == 0) {
            cStringTokenizer tokenizer(par("typeProbability"), ",");
            while (tokenizer.hasMoreTokens()) {
                typeProbability.push_back(std::stod(tokenizer.nextToken()));
            }

            cMessage *prepContractsMsg = new cMessage("prepareContracts");
            scheduleAt(4, prepContractsMsg);
        }
    }

    int getVehicleId(int addr) {
        if (vehicleIdMap.find(addr) != vehicleIdMap.end()) {
            return vehicleIdMap[addr];
        } else {
            cout << "Error in vehicle id map for addr " << addr << endl;
            return -1;
        }
    }

    int myAddress() {
        auto *mac = static_cast<BaseMacLayer *>(getParentModule()->getSubmodule("nic")->getSubmodule("mac1609_4"));
        if (!mac) {
            throw cRuntimeError("MAC module not found");
        }
        return static_cast<int>(mac->myMacAddr);
    }

    cMessage *populate(cMessage *msg, int recipient) {
        BaseMessage *baseMessage = check_and_cast<BaseMessage *>(msg);
        baseMessage->setSender(myAddress());
        baseMessage->setRecipient(recipient);
        baseMessage->setRecipientAddress(recipient);
        baseMessage->setChannelNumber(178);
        baseMessage->setPsid(-1);
        baseMessage->setUserPriority(7);
        return baseMessage;
    }

    bool isForMe(cMessage *msg) {
        BaseMessage *baseMessage = check_and_cast<BaseMessage *>(msg);
        return baseMessage->getRecipient() == myAddress();
    }

    virtual void handleSelfMsg(cMessage *msg) override {
        // Check if this is the 'prepareContracts' self-message
        if (msg->isName("prepareContracts")) {
            prepareContracts(msg);
        } else if (msg->isName("handleTask")) {
            finishTask(msg);
        } else {
            cout << "BS received unknown self message" << endl;
        }

        delete msg;
    }

    virtual void handleLowerMsg(cMessage *msg) override {
        if (isForMe(msg)) {
            if (msg->isName("handleTaskMetadata")) {
                handleTaskMetadata(msg);
            } else if (msg->isName("chooseContract")) {
                chooseContract(msg);
            } else if (msg->isName("handleTask")) {
                handleTask(msg);
            } else if (msg->isName("handleTaskCompletion")) {
                handleTaskCompletion(msg);
            } else {
                cout << "BS received unknown message" << endl;
            }
        }

        delete msg;
    }

    void prepareContracts(cMessage *msg) {
        CURL *curl;
        CURLcode res;
        std::string readBuffer;

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json"); // Add Content-Type header
        // Construct JSON payload
        nlohmann::json data = {
                {"unit_benefit",           unitBenefit},
                {"computation_capability", computationCapability},
                {"duration",               duration},
                {"type_probability",       typeProbability},
                {"total_vehicles",         totalVehicles},
                {"delta_min",              deltaMin},
                {"delta_max",              deltaMax}};
        std::string jsonData = data.dump();

        curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:9090");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, jsonData.size());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            res = curl_easy_perform(curl);

            if (res == CURLE_OK) {
                nlohmann::json responseJson = nlohmann::json::parse(readBuffer);
                sendContractListToVehicles(responseJson);
            } else {
                EV << "curl_easy_perform() failed: " << curl_easy_strerror(res) << endl;
            }

            curl_easy_cleanup(curl);
        }
    }

    void sendContractListToVehicles(const nlohmann::json &responseJson) {
        auto deltas = responseJson["delta"];
        auto pies = responseJson["pie"];

        // Create a new ContractList message
        contractList = new ContractList("processContractList");
        contractList->setContractsArraySize(deltas.size());

        for (size_t i = 0; i < deltas.size(); ++i) {
            Contract contract;
            contract.setResource(deltas[i]);
            contract.setReward(pies[i]);
            contractList->setContracts(i, contract);
        }

        populate(contractList, -1);
        sendDown(contractList->dup());
        cout << "contracts are sent" << endl;
    }

    void chooseContract(cMessage *msg) {
        ContractChoice *choice = check_and_cast<ContractChoice *>(msg);
        vehicleIdMap[choice->getSender()] = choice->getIndex();

        int type = choice->getType();
        int vehicleId = getVehicleId(choice->getSender());

        vehicles[vehicleId].position = choice->getPosition();
        vehicles[vehicleId].speed = choice->getSpeed();

        vehicles[vehicleId].address = choice->getSender();
        if (type < 0) {
            vehicles[vehicleId].sharedResource = 0;
            vehicles[vehicleId].price = 0;
            cout << "Vehicle: " << vehicleId << " has no contract" << endl;
            return;
        }
        vehicles[vehicleId].sharedResource = contractList->getContracts(type).getResource();
        vehicles[vehicleId].price = contractList->getContracts(type).getReward();

        cout << "Vehicle: " << vehicleId << " shared resource: " << vehicles[vehicleId].sharedResource << " price: "
             << vehicles[vehicleId].price << endl;
    }

    void handleTaskMetadata(cMessage *msg) {
        TaskMetadata *taskMetadata = check_and_cast<TaskMetadata *>(msg);

        int vehicleId = getVehicleId(taskMetadata->getSender());
        if (vehicleId == -1) {
            cout << "Vehicle id not found" << endl;
            return;
        }

        cout << "Received task metadata from vehicle: " << vehicleId << endl;
        vehicles[vehicleId].position = taskMetadata->getPosition();
        vehicles[vehicleId].speed = taskMetadata->getSpeed();
        vehicles[vehicleId].taskResource = taskMetadata->getTaskResource();
        vehicles[vehicleId].taskDataSize = taskMetadata->getTaskDataSize();
        vehicles[vehicleId].delayConstraint = taskMetadata->getDelayConstraint();
        vehicles[vehicleId].isTaskReady = true;

        int readyVehiclesCount = getReadyVehiclesCount();
        cout << "Ready vehicles count: " << readyVehiclesCount << endl;
        if (readyVehiclesCount >= taskAssignmentThreshold) {
//            EV << "Vehicle vehicles[20] = {" << endl;
//
//            for (int i = 0; i < numVehicles; i++) {
//                Vehicle vehicle = vehicles[i];
//                EV << "    {"
//                   << vehicle.taskResource << ", "
//                   << vehicle.taskDataSize << ", "
//                   << vehicle.delayConstraint << ", "
//                   << vehicle.sharedResource << ", "
//                   << vehicle.price << ", "
//                   << (vehicle.isTaskReady ? "true" : "false");
//                EV << "}" << endl;
//            }
//
//            EV << "};" << endl;
            assignTasks();
        }
    }

    int getReadyVehiclesCount() {
        int count = 0;
        for (int i = 0; i < numVehicles; i++) {
            if (vehicles[i].isTaskReady) {
                count++;
            }
        }
        return count;
    }

    void assignTasks() {
        cout << "All vehicles are ready, assigning tasks..." << endl;
        int proposals[numVehicles];

        int remainingTasks = 0;
        for (int i = 0; i < numVehicles; i++) {
            proposals[i] = -1;
            vehicles[i].totalTime = new double[numVehicles];
            if (vehicles[i].isTaskReady) {
                remainingTasks++;
            }
        }

        cout << "Remaining tasks: " << remainingTasks << endl;

        int iterations = 0;

        while (remainingTasks > 0) {
            std::vector<int> assignedIds[numVehicles];
            for (int i = 0; i < numVehicles; i++) {
                if (proposals[i] != -1 || !vehicles[i].isTaskReady) {
                    continue;
                }
                double maxPreference = 0;
                int maxPreferenceId = i;
                for (int j = 0; j < numVehicles; j++) {
                    if (i == j || vehicles[j].sharedResource == 0) {
                        continue;
                    }
                    vehicles[i].totalTime[j] = vehicles[i].taskResource / vehicles[j].sharedResource;
                    double transmissionTime = getTransmissionTime(i, j);
                    double transmissionConstraint = getTransmissionConstraint(i, j);
                    if (transmissionConstraint > 0) {
//                        cout << "transmission time from " << i << " to " << j << ": " << transmissionTime << endl;
//                        cout << "transmission constraint from " << i << " to " << j << ": " << transmissionConstraint
//                             << endl;
                    }
                    if (transmissionTime > transmissionConstraint) {
                        continue;
                    }
                    vehicles[i].totalTime[j] += transmissionTime;
//                    cout << "Total time: " << vehicles[i].totalTime[j] << endl;
                    if (vehicles[i].totalTime[j] / 10 > vehicles[i].delayConstraint) {
//                        cout << vehicles[i].totalTime[j] / 10 << " is greater than " << vehicles[i].delayConstraint << endl;
                        continue;
                    }
                    double preference = 1 / vehicles[i].totalTime[j] - vehicles[j].taskPrice;
                    if (preference > maxPreference || maxPreferenceId == i) {
                        maxPreference = preference;
                        maxPreferenceId = j;
                    }
                }
                proposals[i] = maxPreferenceId;
                assignedIds[maxPreferenceId].push_back(i);
                remainingTasks--;
            }
            for (int i = 0; i < numVehicles; i++) {
                if (assignedIds[i].size() > 1) {
                    int skipIndex = -1;
                    if (iterations >= 1000 && iterations % 1000 == 0) {
                        skipIndex = rand() % assignedIds[i].size();
                    }
                    for (int j = 0; j < assignedIds[i].size(); j++) {
                        if (j == skipIndex)
                            continue;
                        int id = assignedIds[i][j];
                        proposals[id] = -1;
                        remainingTasks++;
                    }
                    vehicles[i].taskPrice += getPriceIncrease(i);
                }
            }
            if (iterations % 100 == 0) {
                cout << "Iterations: " << iterations << " RemainingTasks: " << remainingTasks << endl;
            }
            if (iterations > 10000) {
                cout << "Task assignment failed" << endl;
                iterations = 0;
                return;
            }
            iterations++;
        }
        cout << "Task assignment is successful" << endl;

        for (int i = 0; i < numVehicles; i++) {
            if (proposals[i] == -1) {
                continue;
            }
            int nodeId = proposals[i];

            vehicles[nodeId].taskAssignedFrom = i;

            TaskAssignment *taskAssignment = new TaskAssignment("handleTaskAssignment");

            if (nodeId == i) {
                taskAssignment->setFogNodeId(-1);
                taskAssignment->setPrice(0);
                taskAssignment->setAddress(myAddress());

                baseStationTasks++;
            } else {
                taskAssignment->setFogNodeId(nodeId);
                taskAssignment->setPrice(vehicles[nodeId].price);
                taskAssignment->setAddress(vehicles[nodeId].address);
            }

            populate(taskAssignment, vehicles[i].address);
            sendDown(taskAssignment);
        }
    }

    double getDistance(int sourceId, int destinationId) {
        Coord sp = vehicles[sourceId].position;
        Coord dp = vehicles[destinationId].position;

        double distanceX = dp.getX() - sp.getX();
        double distanceY = dp.getY() - sp.getY();
        double distanceZ = dp.getZ() - sp.getZ();
        return sqrt(distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ);
    }

    double getTransmissionTime(int sourceId, int destinationId) {
        return vehicles[sourceId].taskDataSize /
               (3000000 * log(1 + pow(getDistance(sourceId, destinationId), -2) * 0.1));
    }

    double getTransmissionConstraint(int sourceId, int destinationId) {
        Coord sp = vehicles[sourceId].position;
        Coord dp = vehicles[destinationId].position;

        Coord ss = vehicles[sourceId].speed;
        Coord ds = vehicles[destinationId].speed;

        double distanceX = dp.getX() - sp.getX();
        double distanceY = dp.getY() - sp.getY();
        double distanceZ = dp.getZ() - sp.getZ();
//        double distanceZ = 0;
        double distance = sqrt(distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ);

        double relativeSpeedX = ds.getX() - ss.getX();
        double relativeSpeedY = ds.getY() - ss.getY();
        double relativeSpeedZ = ds.getZ() - ss.getZ();
//        double relativeSpeedZ = 0;

        double rangeRadius = 400;

        if (rangeRadius - distance <= 0) {
            return 0;
        }
//        cout << "distance from " << sourceId << " to " << destinationId << " is " << distance << endl;


        double a = relativeSpeedX * relativeSpeedX + relativeSpeedY * relativeSpeedY + relativeSpeedZ * relativeSpeedZ;
        double b = 2 * (distanceX * relativeSpeedX + distanceY * relativeSpeedY + distanceZ * relativeSpeedZ);
        double c = distanceX * distanceX + distanceY * distanceY + distanceZ * distanceZ - rangeRadius * rangeRadius;

        double discriminant = b * b - 4 * a * c;
        if (discriminant < 0) {
            cout << "Discriminant is negative from " << sourceId << " to " << destinationId << endl;
            return 1000000;
        } else if (a == 0) {
            return 1000000;
        } else {
            return (-b + sqrt(discriminant)) / (2 * a);
        }
    }

    double getPriceIncrease(int id) {
        return 0.001;
    }

    void handleTaskCompletion(cMessage *msg) {
        TaskCompletion *taskCompletion = (check_and_cast<TaskCompletion *>(msg))->dup();
        int vehicleId = getVehicleId(taskCompletion->getSender());

        int assignedFrom = vehicles[vehicleId].taskAssignedFrom;

        populate(taskCompletion, vehicles[assignedFrom].address);
        sendDown(taskCompletion);
    }

    void handleTask(cMessage *msg) {
        Task *task = check_and_cast<Task *>(msg);

        // sleep for sharedResource / taskResource
        simtime_t sleepTime = task->getTaskResource() / (computationCapability / baseStationTasks);

        cout << "BaseStation received task with resource " << task->getTaskResource() <<
             " at " << simTime() << " and sleeping for " << sleepTime << endl;

        scheduleAt(simTime() + sleepTime, task->dup());
    }

    void finishTask(cMessage *msg) {
        Task *task = check_and_cast<Task *>(msg);
        cout << "BaseStation finished task with resource " << task->getTaskResource() <<
             " at " << simTime() << endl;

        // send task completion to base station
        TaskCompletion *taskCompletion = new TaskCompletion("handleTaskCompletion");
        taskCompletion->setResult("Task completed");

        populate(taskCompletion, task->getSender());
        sendDown(taskCompletion);
    }
};

Define_Module(BaseStation);
