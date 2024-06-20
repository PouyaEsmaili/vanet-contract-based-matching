package contractbased;

import org.car2x.veins.base.modules.IBaseApplLayer;

simple BaseStation like IBaseApplLayer {
    parameters:
        @class(BaseStation);
        int headerLength = default(88bit) @unit(bit); //header length of the application

        double unitBenefit;
        double computationCapability;
        int duration;
        string typeProbability;
        int totalVehicles;
        double deltaMin;
        double deltaMax;

        int taskAssignmentThreshold;
    gates:
        input lowerLayerIn; // from mac layer
        output lowerLayerOut; // to mac layer
        input lowerControlIn;
        output lowerControlOut;
}