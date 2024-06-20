package contractbased;

import org.car2x.veins.base.modules.IBaseApplLayer;

simple Vehicle like IBaseApplLayer {
    parameters:
        @class(Vehicle);
        int headerLength = default(88bit) @unit(bit); //header length of the application

        double totalResource; // Total Available Computation Resource (delta m,0)
        double taskDataSize; // (D)
        double taskResource; // (C)
        double delayConstraint; // (tao)
    gates:
        input lowerLayerIn; // from mac layer
        output lowerLayerOut; // to mac layer
        input lowerControlIn;
        output lowerControlOut;
}