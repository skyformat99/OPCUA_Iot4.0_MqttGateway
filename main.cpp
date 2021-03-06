#include <QCoreApplication>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include "open62541.h"
#include "mqtt/async_client.h"

using namespace std;
using namespace mqtt;

async_client client("tcp://localhost:1883", "GW", "data-persist");

/*----------------------------OPCUA---------------------------------------*/
static UA_StatusCode
car1Callback(UA_Server *server,
                         const UA_NodeId *sessionId, void *sessionHandle,
                         const UA_NodeId *methodId, void *methodContext,
                         const UA_NodeId *objectId, void *objectContext,
                         size_t inputSize, const UA_Variant *input,
                         size_t outputSize, UA_Variant *output) {

    UA_String tmp = UA_STRING_ALLOC("mode1");

    /*add UARM communication*/
    //system("mosquitto_pub -h localhost -m 1 -t /production/order");
    client.publish("/production/order","1", sizeof("1"));

    UA_Variant_setScalarCopy(output, &tmp, &UA_TYPES[UA_TYPES_STRING]);
    UA_String_deleteMembers(&tmp);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "car 1 in production");
    return UA_STATUSCODE_GOOD;
}

static UA_StatusCode
car2Callback(UA_Server *server,
                         const UA_NodeId *sessionId, void *sessionHandle,
                         const UA_NodeId *methodId, void *methodContext,
                         const UA_NodeId *objectId, void *objectContext,
                         size_t inputSize, const UA_Variant *input,
                         size_t outputSize, UA_Variant *output) {
    UA_String tmp = UA_STRING_ALLOC("mode2");

    /*add UARM communication*/
    //system("mosquitto_pub -h localhost -m 2 -t /production/order");
    client.publish("/production/order","2", sizeof("2"));

    UA_Variant_setScalarCopy(output, &tmp, &UA_TYPES[UA_TYPES_STRING]);
    UA_String_deleteMembers(&tmp);
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "car 2 in production");
    return UA_STATUSCODE_GOOD;
}

static void
addUarmMethod(UA_Server *server) {

    // Input Arguments
    UA_Argument inputArguments;
    UA_Argument_init(&inputArguments);
    inputArguments.arrayDimensionsSize = 0;
    inputArguments.arrayDimensions = NULL;
    inputArguments.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
    inputArguments.description = UA_LOCALIZEDTEXT("en_US", "A String");
    inputArguments.name = UA_STRING("MyInput");
    inputArguments.valueRank = -1;

    // Output Arguments
    UA_Argument outputArguments;
    UA_Argument_init(&outputArguments);
    outputArguments.arrayDimensionsSize = 0;
    outputArguments.arrayDimensions = NULL;
    outputArguments.dataType = UA_TYPES[UA_TYPES_STRING].typeId;
    outputArguments.description = UA_LOCALIZEDTEXT("en_US", "A String");
    outputArguments.name = UA_STRING("MyOutput");
    outputArguments.valueRank = -1;

    // Attributes
    UA_MethodAttributes incAttr;
    UA_MethodAttributes_init(&incAttr);
    incAttr.description = UA_LOCALIZEDTEXT("en_US", "Move UARM");
    incAttr.displayName = UA_LOCALIZEDTEXT("en_US", "Move UARM");
    incAttr.executable = true;
    incAttr.userExecutable = true;

    // Add MethodNode to UARM object
    UA_Server_addMethodNode(server, UA_NODEID_STRING(1, "car1"),
                            UA_NODEID_STRING(1, "UARM"),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                            UA_QUALIFIEDNAME(1, "car1"),
                            incAttr, &car1Callback,
                            1, &inputArguments, 1, &outputArguments, NULL, NULL);

    // Add MethodNode to UARM object
    UA_Server_addMethodNode(server, UA_NODEID_STRING(1, "car2"),
                            UA_NODEID_STRING(1, "UARM"),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_HASCOMPONENT),
                            UA_QUALIFIEDNAME(1, "car2"),
                            incAttr, &car2Callback,
                            1, &inputArguments, 1, &outputArguments, NULL, NULL);
}

static void
addUarmObject(UA_Server *server) {
    const UA_NodeId uarmNodeId =  UA_NODEID_STRING(1, "UARM");
    UA_ObjectAttributes oAttr = UA_ObjectAttributes_default;
    oAttr.displayName = UA_LOCALIZEDTEXT("en-US", "UARM");
    UA_Server_addObjectNode(server, uarmNodeId,
                            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER),
                            UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
                            UA_QUALIFIEDNAME(1, "UARM"),UA_NODEID_NUMERIC(0, UA_NS0ID_BASEOBJECTTYPE),
                            oAttr, NULL, NULL);
    addUarmMethod(server);
}


/*-------------------------------------mqtt-----------------------------------*/

class lo_callback : public virtual mqtt::callback
{
    mqtt::async_client& client_;

    void connected(const std::string& cause) override {
        std::cout << "\nConnected: " << cause << std::endl;
        client_.subscribe("ready", 1);
        std::cout << std::endl;
    }

    // Callback for when the connection is lost.
    // This will initiate the attempt to manually reconnect.
    void connection_lost(const std::string& cause) override {
        std::cout << "\nConnection lost";
        if (!cause.empty())
            std::cout << ": " << cause << std::endl;
        std::cout << std::endl;
    }

    // Callback for when a message arrives.
    void message_arrived(mqtt::const_message_ptr msg) override {
        UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "got message:");
        std::cout << msg->get_topic() << ": " << msg->get_payload_str() << std::endl;

        //Send answer to OPC UA Client
    }

    void delivery_complete(mqtt::delivery_token_ptr token) override {}

public:
    lo_callback(mqtt::async_client& client) : client_(client) {}
};


UA_Boolean running = true;
static void stopHandler(int sign) {
    UA_LOG_INFO(UA_Log_Stdout, UA_LOGCATEGORY_SERVER, "received ctrl-c");
    running = false;
}


int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);

    signal(SIGINT, stopHandler);
    signal(SIGTERM, stopHandler);

    UA_ServerConfig *config = UA_ServerConfig_new_default();
    UA_Server *server = UA_Server_new(config);

    addUarmObject(server);

    lo_callback cb(client);
    client.set_callback(cb);
    connect_options connOpts =connect_options();

    try{
        // Connect to the MQTT broker
        client.connect(connOpts)->wait();
        client.publish("/gw/ready","1", sizeof("1"));
        client.subscribe("/mr/status",1);
        client.start_consuming();

    }catch (const mqtt::exception& exc) {
        cerr << exc.what() << endl;
        return 1;
    }

    UA_StatusCode retval = UA_Server_run(server, &running);
    client.stop_consuming();
    client.disconnect()->wait();


    UA_Server_delete(server);
    UA_ServerConfig_delete(config);
    return (int)retval;
}
