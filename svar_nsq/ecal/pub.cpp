#include "Messenger.h"
#include "Glog.h"
#include "Svar.h"

#include <ecal/ecal.h>
#include <ecal/ecal_callback.h>
#include <ecal/ecal_core.h>
#include <ecal/ecal_publisher.h>
#include <ecal/ecal_subscriber.h>
#include <ecal/msg/protobuf/subscriber.h>
#include <ecal/msg/string/publisher.h>

#include <cstddef>
#include <memory>
#include <regex>
#include <string>
#include <thread>

using namespace sv;
using namespace GSLAM;

class MessengerECAL{
public:
    MessengerECAL(sv::Svar config)
    {
        eCAL::Initialize(0, nullptr, "eCAL-pub");
        server=config.arg<std::string>("server","127.0.0.1:4150","the ecald tcp server address");
        matcher=config.arg<std::string>("pattern","^@(.+)$","The regex pattern for matching topic name");
        std::string serializer_name=config.arg<std::string>("serializer","json","The serializer typename: json, cbor");

        serializer=svar["serializers"][serializer_name];

        if(serializer.isUndefined()){
            LOG(FATAL) <<"Unkown serializer "<<serializer_name;
        }
    }

    ~MessengerECAL(){
        for(auto& pub: publishers) pub.second->Destroy();
        for(auto& sub: subscribers) sub.second->Destroy();
        eCAL::Finalize();
    }

    void publish(std::string topic, sv::Svar msg){
        //sv::SvarBuffer buf=serializer.call("dump",msg).castAs<sv::SvarBuffer>();
        std::string buf = msg.dump_json();
        std::cout << "publish to ecal: " << buf << std::endl;
        publishers[topic]->Send(buf);
    }

    void advertise(std::string topic, sv::Svar msg) {
        if (!publishers.count(topic))
            publishers[topic] = std::make_shared<eCAL::CPublisher>(topic);
    }

    sv::Svar subscribe(std::string topic, const sv::SvarFunction& callback){
        auto ret= std::make_shared<eCAL::CSubscriber>(topic.c_str());
        sv::Svar cbk=callback;
        eCAL::ReceiveCallbackT cb = [cbk, this](const char* topic_name, const struct eCAL::SReceiveCallbackData* data_){
            Svar msg1=serializer.call("load",sv::SvarBuffer(data_->buf,data_->size));
            cbk(msg1.dump_json());
        };
        ret->AddReceiveCallback(cb);
        std::cout << "topic " << topic << " subscribed from ecal.\n";
        subscribers[topic] = ret;
        return ret;
    }

    std::string filter_name(std::string topic,bool silent=false){
        std::smatch result;
        if(!std::regex_match(topic,result,matcher))
        {
            CHECK(silent)<<"Topic "<<topic<<" not match regex " << config.get<std::string>("patten","^@(.+)$");
            return "";
        }

        return result[1];
    }

    sv::Svar        config;
    std::string     server;

    std::map<std::string, std::shared_ptr<eCAL::CPublisher>> publishers;
    std::map<std::string, std::shared_ptr<eCAL::CSubscriber>> subscribers;

    std::thread     worker;
    std::regex      matcher;
    sv::Svar        serializer;
};

class BridgeECAL{
public:
    BridgeECAL(sv::Svar config)
        : ecal(config),subs(Svar::object()){
        std::string patten=config.get<std::string>("patten","@");

        data["newpub"]=GSLAM::Messenger::instance().subscribe("messenger/newpub",0,[this](GSLAM::Publisher pub){
            std::string ecal_topic=ecal.filter_name(pub.getTopic(),true);
            if(ecal_topic.empty()) return;
            reassignPublisher(pub,pub.getTopic());
        });

        data["newsub"]=GSLAM::Messenger::instance().subscribe("messenger/newsub",0,[this,patten](GSLAM::Subscriber sub){
            std::string ecal_topic=ecal.filter_name(sub.getTopic(),true);
            if(ecal_topic.empty()) return;
            reassignSubscriber(sub.getTopic(),ecal_topic);
        });
    }

    void reassignPublisher(GSLAM::Publisher pub, std::string topic){
        std::cerr << "reassign pub: " << topic << std::endl;
        ecal.publishers[topic] = std::make_shared<eCAL::CPublisher>(topic);
        pub.impl_->pubFunc=[this,topic](const sv::Svar& msg){
            ecal.publish(topic,msg.dump_json());
        };
    }

    void reassignSubscriber(std::string msg_topic, std::string ecal_topic){
        std::cerr << "reassign sub: " << msg_topic << std::endl;
        if(subs.exist(msg_topic)) return;
        sv::Svar subecal=ecal.subscribe(msg_topic,[msg_topic](const std::string& msg){
            Messenger::instance().publish(msg_topic,Svar(msg));
        });
        subs[msg_topic]=subecal;
    }

    MessengerECAL  ecal;
    sv::Svar       subs,data;
};

REGISTER_SVAR_MODULE(ecal){
    Class<MessengerECAL>("MessengerECAL")
            .unique_construct<Svar>()
            .def("publish",&MessengerECAL::publish)
            .def("subscribe",&MessengerECAL::subscribe);

    Class<BridgeECAL>("BridgeECAL")
            .unique_construct<Svar>();

    Class<Messenger>("Messenger")
            .construct<>()
            .def_static("instance",&Messenger::instance)
            .def("getPublishers",&Messenger::getPublishers)
            .def("getSubscribers",&Messenger::getSubscribers)
            .def("introduction",&Messenger::introduction)
            .def("advertise",[](Messenger msg,const std::string& topic,int queue_size){
        return msg.advertise<sv::Svar>(topic,queue_size);
    })
    .def("subscribe",[](Messenger msger,
         const std::string& topic, int queue_size,
         const SvarFunction& callback){
        return msger.subscribe(topic,queue_size,callback);
    })
    .def("publish",[](Messenger* msger,std::string topic,sv::Svar msg){return msger->publish(topic,msg);});

    Class<Publisher>("Publisher")
            .def("shutdown",&Publisher::shutdown)
            .def("getTopic",&Publisher::getTopic)
            .def("getTypeName",&Publisher::getTypeName)
            .def("getNumSubscribers",&Publisher::getNumSubscribers)
            .def("publish",[](Publisher* pubptr,sv::Svar msg){return pubptr->publish(msg);});

    Class<Subscriber>("Subscriber")
            .def("shutdown",&Subscriber::shutdown)
            .def("getTopic",&Subscriber::getTopic)
            .def("getTypeName",&Subscriber::getTypeName)
            .def("getNumPublishers",&Subscriber::getNumPublishers);

//    auto global = sv::Registry::load("svar_messenger");
//    auto msg = global["messenger"];

//    if (!global["logger"].isUndefined())
//    {
//        GSLAM::getLogSinksGlobal() = global["logger"].as<std::shared_ptr<std::set<GSLAM::LogSink *>>>();
//    }
//    else
//        global["logger"] = GSLAM::getLogSinksGlobal();

//    if (msg.is<GSLAM::Messenger>())
//    {
//        GSLAM::Messenger::instance() = msg.as<GSLAM::Messenger>();
//    }
//    svar["messenger"] = Messenger::instance();
}

EXPORT_SVAR_INSTANCE

int run(sv::Svar config){
    BridgeECAL bridge(config);

    MessengerECAL& ecal=bridge.ecal;

    std::string topic=config.arg<std::string>("topic","@test","the topic for pubsub");
    int sleepTime=config.arg("sleep",5,"the time to sleep");

    if(config.get("help",false)) return config.help();

    auto subecal=ecal.subscribe(topic,[](sv::Svar msg){
            std::cerr<<"received by ecal:"<<msg<<std::endl;
    });
    std::cout << "1 --- subscriber number of " << topic << ": " << messenger.getSubscribers() << std::endl;

    auto subscriber=messenger.subscribe(topic,0,[](sv::Svar msg){
        std::cerr<<"received by native messenger:"<<msg<<std::endl;
    });
    std::cout << "subscribe of " << topic << " initialized.\n";
    auto publisher=messenger.advertise(topic,0);
    std::cout << "2 --- subscriber number of " << topic << ": " << messenger.getSubscribers() << std::endl;
    int n = 0;
    while (n < 20)
    {
    publisher.publish(nullptr);
    publisher.publish(true);
    publisher.publish(1);
    publisher.publish(3.14);
    publisher.publish("hello world");
    publisher.publish(sv::Svar({1,2,"hello"}));
    sleep(1);
        ++n;
    }
    
    publisher.publish(nullptr);
    publisher.publish(true);
    publisher.publish(1);
    publisher.publish(3.14);
    publisher.publish("hello world");
    publisher.publish(sv::Svar({1,2,"hello"}));

    bridge.ecal.publish(topic,"publish from ecal");
    sleep(sleepTime);

    if(config.Get<std::string>("serializer")=="cbor"){
        auto subObject=messenger.subscribe("@buffer",0,[](sv::SvarBuffer buf){
            LOG(INFO)<<"Received buffer "<<sv::Svar(buf);
        });

        auto publisher=messenger.advertise("@buffer",0);
        publisher.publish(sv::SvarBuffer(1024));
        sleep(sleepTime);
    }
    sleep(sleepTime);
    std::cout << "over...\n";
    int count = 0;
    while (eCAL::Ok() && count < 10000){
        ecal.publish(topic, "loop " + std::to_string(count));
        ++count;
        eCAL::Process::SleepMS(sleepTime);
    }
    return 0;
}

int main(int argc,char** argv){
    svar.parseMain(argc,argv);

      return run(svar);
}
