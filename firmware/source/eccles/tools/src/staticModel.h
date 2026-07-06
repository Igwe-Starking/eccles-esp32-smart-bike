/*
    base class packager for static models,this class creates a cpp header file to index
    its model data unlike dynamic model which packages the model information inside the model itself
*/

#ifndef ECCLES_STATIC_MODEL
#define ECCLES_STATIC_MODEL

#include "Model.h"

ECCLES_API {

    class staticModel : public Model {
        public:

        e_boolean pack() override;
        e_boolean extract() override;
    };

};

#endif // ECCLES_STATIC_MODEL
