import veins.base.utils.SimpleAddress;
import veins.modules.messages.BaseFrame1609_4;

message Coord {
    double x;
    double y;
    double z;
}

message BaseMessage extends veins::BaseFrame1609_4 {
    int sender = -1;
    int recipient = -1;
}

message BaseMessageWithGeo extends BaseMessage {
    Coord position;
    Coord speed;
}

message Contract {
    double resource; // Required Computation Resource (delta)
    double reward; // Corresponding Reward (pi)
}

message ContractList extends BaseMessage {
    Contract contracts[];
}

message ContractChoice extends BaseMessageWithGeo {
    int type;
    int index;
}

message TaskMetadata extends BaseMessageWithGeo {
    double taskResource;
    double taskDataSize;
    double delayConstraint;
}

message TaskAssignment extends BaseMessage {
    int fogNodeId;
    double price;
    int address;
}

message Task extends BaseMessage {
    string taskData;
    double taskResource;
}

message TaskCompletion extends BaseMessage {
    string result;
}