#include <omnetpp.h>

#include "veins/base/modules/BaseApplLayer.h"
#include "veins/base/modules/BaseMacLayer.h"
#include "veins/modules/mobility/traci/TraCIMobility.h"
#include "message_m.h"

using namespace std;
using namespace omnetpp;

class Vehicle : public veins::BaseApplLayer {
private:
    double totalResource;
    double taskDataSize;
    double taskResource;
    double delayConstraint;
    int baseStationAddress;
    Contract selectedContract;

    SimTime taskAssignmentTime;

    veins::TraCIMobility *mobility;

protected:
    virtual void initialize(int stage) override {
        BaseApplLayer::initialize(stage);
        // get parameters
        totalResource = par("totalResource");
        taskDataSize = par("taskDataSize");
        taskResource = par("taskResource");
        delayConstraint = par("delayConstraint");
        baseStationAddress = 0;
        selectedContract = Contract();

        mobility = veins::TraCIMobilityAccess().get(getParentModule());

        if (stage == 0)
            cout << "Car initialized with id " << getParentModule()->getIndex() << " at " << simTime() << endl;
    }

    virtual void finish() override {
        BaseApplLayer::finish();
    }

    int getIndex() {
        return getParentModule()->getIndex();
    }

    int myAddress() {
        auto *mac = static_cast<veins::BaseMacLayer *>(getParentModule()->getSubmodule("nic")->getSubmodule(
                "mac1609_4"));
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
        return baseMessage->getRecipient() == myAddress() || baseMessage->getRecipient() == -1;
    }

    Coord veinsCoordToCoord(veins::Coord veinsCoord) {
        Coord coord = Coord();
        coord.setX(veinsCoord.x);
        coord.setY(veinsCoord.y);
        coord.setZ(veinsCoord.z);
        return coord;
    }

    virtual void handleSelfMsg(cMessage *msg) override {
        if (msg->isName("prepareTaskMetadata")) {
            prepareTaskMetadata();
        } else if (msg->isName("handleTask")) {
            finishTask(msg);
        }
    }

    virtual void handleLowerMsg(cMessage *msg) override {
        if (isForMe(msg)) {
            // check if cMessage is of type ContractList
            if (msg->isName("processContractList")) {
                handleContractList(msg);
            } else if (msg->isName("handleTask")) {
                handleTask(msg);
            } else if (msg->isName("handleTaskAssignment")) {
                handleTaskAssignment(msg);
            } else if (msg->isName("handleTaskCompletion")) {
                handleTaskCompletion(msg);
            } else {
                cout << "Vehicle: " << myAddress() << " received unknown message" << endl;
            }
        }

        delete msg;
    }

    virtual void handleLowerControl(cMessage *msg) override {
        cout << "Vehicle: " << getIndex() << " received control message with name " << msg->getName() << endl;
        delete msg;
    }

    void handleContractList(cMessage *msg) {
        // cast cMessage to ContractList
        EV << "Vehicle: " << myAddress() << " with resource: " << totalResource << " received contract list" << endl;
        ContractList *contractList = check_and_cast<ContractList *>(msg);
        baseStationAddress = contractList->getSender();

        int bestContractIndex = -1;
        for (int i = 0; i < contractList->getContractsArraySize(); i++) {
            // get contract from contractList
            Contract contract = contractList->getContracts(i);

            if (contract.getResource() <= totalResource && contract.getReward() >= selectedContract.getReward()) {
                selectedContract = contract;
                bestContractIndex = i;
            }
        }
        ContractChoice *contractChoice = new ContractChoice("chooseContract");
        contractChoice->setType(bestContractIndex);
        // print contract choice index, resource, and reward, also with index of vehicle
        EV << "Contract choice: " << contractChoice->getType() << " from vehicle: " << myAddress()
           << " with resource: "
           << selectedContract.getResource() << " and reward: " << selectedContract.getReward() << endl;

        populate(contractChoice, baseStationAddress);
        sendDown(contractChoice);

        cMessage *prepTaskMetadataMsg = new cMessage("prepareTaskMetadata");
        scheduleAt(simTime() + uniform(0, 0.1), prepTaskMetadataMsg);
    }

    void prepareTaskMetadata() {
        if (totalResource > 0)
            return;

        cout << "Vehicle: " << getIndex() << " with resource: " << totalResource << " preparing task metadata" << endl;

        veins::Coord position = mobility->getPositionAt(simTime());
        veins::Coord speed = mobility->getHostSpeed();

        TaskMetadata *taskMetadata = new TaskMetadata("handleTaskMetadata");
        taskMetadata->setTaskResource(taskResource);
        taskMetadata->setTaskDataSize(taskDataSize);
        taskMetadata->setDelayConstraint(delayConstraint);
        taskMetadata->setPosition(veinsCoordToCoord(position));
        taskMetadata->setSpeed(veinsCoordToCoord(speed));

        populate(taskMetadata, baseStationAddress);
        sendDown(taskMetadata);
    }

    void handleTaskAssignment(cMessage *msg) {
        TaskAssignment *task = check_and_cast<TaskAssignment *>(msg);
        cout << "Vehicle: " << getIndex() << " will assign it's task to " << task->getFogNodeId() <<
             " with price " << task->getPrice() << " with resource " << taskResource << " data size " << taskDataSize
             << endl;

        offloadTask(task->getAddress());
    }

    void offloadTask(int address) {
        string *taskData = new string(taskDataSize, 'a');
        cout << "Vehicle: " << getIndex() << " offloading task to " << address <<
             " and data size " << taskDataSize << " at " << simTime() << endl;

        taskAssignmentTime = simTime();

        Task *task = new Task("handleTask");
        task->setTaskData(taskData->c_str());
        task->setTaskResource(taskResource);

        populate(task, address);
        sendDown(task);
    }

    void handleTask(cMessage *msg) {
        Task *task = check_and_cast<Task *>(msg);
        cout << "Vehicle: " << getIndex() << " received task with resource " << task->getTaskResource() <<
             " at " << simTime() << endl;

        // sleep for sharedResource / taskResource
        simtime_t sleepTime = task->getTaskResource() / selectedContract.getResource();
        cout << "Vehicle: " << getIndex() << " sleeping for " << sleepTime << " at " << simTime() << endl;

        scheduleAt(simTime() + sleepTime, task);
    }

    void finishTask(cMessage *msg) {
        Task *task = check_and_cast<Task *>(msg);
        cout << "Vehicle: " << getIndex() << " finished task with resource " << task->getTaskResource() <<
             " at " << simTime() << endl;

        delete task;

        // send task completion to base station
        TaskCompletion *taskCompletion = new TaskCompletion("handleTaskCompletion");
        taskCompletion->setResult("Task completed");

        populate(taskCompletion, baseStationAddress);
        sendDown(taskCompletion);
    }

    void handleTaskCompletion(cMessage *msg) {
        TaskCompletion *taskCompletion = check_and_cast<TaskCompletion *>(msg);
        cout << "Vehicle: " << getIndex() << " received task completion with result " << taskCompletion->getResult()
             << " at " << simTime() << endl;

        SimTime delay = simTime() - taskAssignmentTime;
        cout << "Vehicle: " << getIndex() << " task delay: " << delay << " at " << simTime() << endl;
    }
};

Define_Module(Vehicle);
