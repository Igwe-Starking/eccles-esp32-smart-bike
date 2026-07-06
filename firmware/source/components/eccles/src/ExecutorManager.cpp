//Base implementation of executormanager
//pure C++ logic, no framework dependency

#include "Executors.h"

ECCLES_API {

    //prepare any executor that need preparation
    void ExecutorManager::prepare(){
        //initialize the executor mutex
        Executor::start(); //very very important or else all obtain and send will fail silently
        //prepare configuration
        cnf.open();
    }

    //void sets executors result handler
    void ExecutorManager::setResultHandler(ResultHandler* h){
        Executor::setHandler(h);
    }

    //run the executor command pool
    void ExecutorManager::run(){
        Executor::checkIncomingCommands();
        dv.runMonitor();
    }

    //unused for now,kept for api symmetry with prepare()
    void ExecutorManager::cleanup(){
    }
};
