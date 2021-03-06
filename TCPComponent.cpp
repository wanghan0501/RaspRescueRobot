//
//  TCPComponent.cpp
//  rescueRobot
//
//  Created by 黄正跃 on 25/09/2016.
//  Last Modified by Wang han on 11/11/2016
//  Copyright © 2016 黄正跃. All rights reserved.
//

#include <string>
#include <string.h>
#include <iomanip>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fstream>

#include "TCPComponent.h"
#include "Services.h"
#include "CarHardware.h"

#define DEBUG

namespace rr{

    TCPComponent* TCPComponent::tcpComponent;

    std::thread* TCPComponent::sendThread = nullptr;

    //std::string loginName = "";
    //std::string password = "";

    TCPComponent::TCPComponent(const char* serviceAdrress,const int servicePort,const int workingPort):
    recieveThreadRun(true),
    loginState(false),
    receiveThread(nullptr),
    sockfd(-1) {


         //get login data
         this->getLoginInfo();

        //create tcp socket
        if(-1 == (sockfd = socket(AF_INET, SOCK_STREAM, 0))){
            std::cerr<<"socket initial failed\n";
        }

        //close TIME_WAIT state
        int option = 1;
        if (setsockopt (sockfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)) < 0) {
            throw std::runtime_error("set socket option failed!");
        }

        //set the addr of the local socket
        bzero(&workingAddr, sizeof(sockaddr_in));
        workingAddr.sin_family=AF_INET;
        workingAddr.sin_port=htons(workingPort);
        workingAddr.sin_addr.s_addr = htonl (INADDR_ANY);
        //bind
        if(-1 == (bind(sockfd, (sockaddr *)&workingAddr, sizeof(workingAddr)))){
            close(sockfd);
            sockfd = -2;
            throw std::runtime_error("socket bind failed!");
            //std::cerr<<"socket bind failed\n";
        }
        //set the address of the remote server
        bzero(&serverAddr, sizeof(sockaddr_in));
        serverAddr.sin_family=AF_INET;
        serverAddr.sin_port=htons(servicePort);
        serverAddr.sin_addr.s_addr = inet_addr(serviceAdrress);
        //connect
        if(-1 == connect(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr))){
            //close(sockfd);
            //sockfd = -3;
            std::cerr<<"socket connect failed"<<std::endl;
            this->reconnection();
        }
    }

    void TCPComponent::getLoginInfo(){
         Json::Value fileRoot;
         Json::Reader fileReader;
         std::ifstream loginData;

         loginData.open("../.loginJson");
         if (!loginData) {
            throw std::runtime_error("login json doesn't exist!");
         }

         if (fileReader.parse(loginData,fileRoot)){
             loginName = fileRoot["loginName"].asString();
             password = fileRoot["password"].asString();
         }

         #ifdef DEBUG
         std::cout << "loginName : "<<loginName<<std::endl;
         std::cout << "password : "<<password<<std::endl;
         std::cout <<"In "<<__FILE__<<" at "<<__LINE__<<" line."<<std::endl;
         #endif

         loginData.close();
    }

    void TCPComponent::login(){
     //create json data

         Json::Value root;
         Json::FastWriter writer;
         root["requestType"] = Json::Value("login");
         root["loginName"] = Json::Value(loginName);
         root["password"] = Json::Value(password);
         std::string RequestJson = writer.write(root); //include '\n'

         //just for debug
         //std::cout <<"RequestJson :"<<RequestJson<<std::endl;
         //std::cout <<"RequestJson.length : "<<RequestJson.length()<<std::endl;

         // 发送登录请求
         sendRequest(RequestJson.c_str(),RequestJson.length()-1);

         Services& services = Services::getInstance();

         //check login info
         char* headBuffer = new char[5];
         //读取头部信息(type,length) 共5byte
         read(this->sockfd,headBuffer, 5);
         char* dataBuffer = new char[*((int*)(headBuffer+1))];
         read(this->sockfd,dataBuffer,*((int*)(headBuffer+1)));

         //just for debug
         #ifdef DEBUG
         printf("headBuffer : %d %d %d %d %d\n", headBuffer[0],headBuffer[1],headBuffer[2],headBuffer[3],headBuffer[4]);
         std::cout << dataBuffer<<std::endl;
         std::cout <<"In "<<__FILE__<<" at "<<__LINE__<<" line."<<std::endl;
         #endif

         if(headBuffer[0]=='m'){
             std::string status ="";
             std::string ResponseJson = std::string(dataBuffer);
             Json::Reader reader;
             Json::Value root;
             //parse the response json
             if (reader.parse(ResponseJson, root)) {
                 status = root["success"].asString();

                 //just for debug
                 #ifdef DEBUG
                 std::cout <<"status is "<<status<<std::endl;
                 std::cout <<"In "<<__FILE__<<" at "<<__LINE__<<" line."<<std::endl;
                 #endif
             }

             if (status == "true"){
                 this->loginState = true;
                 //login success, then start the receiveThread
                 this->receiveThread = new std::thread(receive, this);
                 this->receiveThread->join();

             }else{

             }
         }

         delete[] headBuffer;
         headBuffer = nullptr;
         delete[] dataBuffer;
         dataBuffer = nullptr;
         std::cerr << "No ResponseJson Receive!"<<std::endl;
    }


    //reconnection server
    void TCPComponent::reconnection(){
        int connectState = -1;
        while(!this->loginState) {
            //wait 10s, then try again
            sleep(10);

            connectState = connect(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr));
            if(connectState != -1){
                std::cout<<"socket connect successed."<<std::endl;
            }else{
                std::cerr<<"socket connect failed, after 10s try to connect again."<<std::endl;
            }
            //try logining again
            if (connectState != -1) {
                this->login();
            }
        }
    }

    //stop connection
    void TCPComponent::stopConnection(){
        close(sockfd);
        std::cout <<"socket has been closed."<<std::endl;
    }

    void TCPComponent::receive(TCPComponent *that) {
        Services& services = Services::getInstance();

        while (that->recieveThreadRun){

            char* headBuffer = new char[5];
            //读取头部信息(type,length) 共5byte
            read(that->sockfd,headBuffer, 5);
            char* dataBuffer = new char[*((int*)(headBuffer+1))];
            read(that->sockfd,dataBuffer,*((int*)(headBuffer+1)));

            //debug
            #ifndef DEBUG
            printf("headBuffer : %d %d %d %d %d\n", headBuffer[0],headBuffer[1],headBuffer[2],headBuffer[3],headBuffer[4]);
            std::cerr << dataBuffer<<std::endl;
            std::cerr <<"In "<<__FILE__<<" at "<<__LINE__<<" line."<<std::endl;
            #endif

            // (注意：服务器返回的消息type='m' 内容为json, 控制端发送的消息type ='c'表示命令)
            if(headBuffer[0]=='c'){
                 float x_offset=*((float*)dataBuffer);
                 float y_offset=*((float*)dataBuffer+1);
                 //debug
                 //#ifndef DEBUG
                 std::cerr << "x_offset:"<< x_offset <<"\ty_offset: "<< y_offset <<std::endl;
                 std::cerr <<"In "<<__FILE__<<" at "<<__LINE__<<" line."<<std::endl;
                 //#endif
                 //初始化硬件设备
                 if (!services.hardwareIsStarted()){
                     services.startMovementHardware();
                 }
                 if(x_offset>0.1){
                     services.goOneStep(RASPBERRY_ROBOT_DIRECTION_RIGHT);
                 }
                 else if(x_offset<-0.1){
                     services.goOneStep(RASPBERRY_ROBOT_DIRECTION_LEFT);
                 }
                 if(y_offset>0.1){
                     services.goOneStep(RASPBERRY_ROBOT_DIRECTION_FORWARD);   
                 }
                 else if(y_offset<-0.1){
                     services.goOneStep(RASPBERRY_ROBOT_DIRECTION_BACK);
                 }
            }
            else if(headBuffer[0]=='m'){
                 std::string action = "";
                 std::string error = "";
                 std::string ResponseJson = std::string(dataBuffer);
                 Json::Reader reader;
                 Json::Value root;
                 int height = 0;
                 int width = 0;
                 int brightness = 0;
                 int contrast = 0;
                 int saturation = 0;

                 //deal with root
                 if (reader.parse(ResponseJson, root)) {
                     action = root["action"].asString();
                     error = root["error"].asString();
                 }

                 if (action == "startVideo") {
                    if (services.streamerIsStarted())
                         //start the sendThread
                         std::cerr<<"streamer had been started!"<<std::endl;
                    else{
                         sendThread = new std::thread(&Services::startVedioStreamer,&services);
                         std::cout<<"video streamer started!"<<std::endl;
                    }
                }else if(action == "stopVideo") {
                    services.stopVedioStreamer();
                    std::cout<<"video streamer stoped!"<<std::endl;
                }else if (action == "configuration") {
                     if (reader.parse(ResponseJson, root)) {
                         height = root["height"].asInt();
                         width = root["width"].asInt();
                         brightness = root["brightness"].asInt();
                         contrast = root["contrast"].asInt();
                         saturation = root["saturation"].asInt();

                     }

                     #ifdef DEBUG
                     //std::cout <<"image height as : " << height << std::endl;
                     //std::cout <<"image width as : " << width << std::endl;
                     std::cout <<"image brightness as : " << brightness << std::endl;
                     std::cout <<"image contrast as : " << contrast << std::endl;
                     std::cout <<"image saturation as : " << saturation << std::endl;
                     std::cout <<"In "<<__FILE__<<" , at "<<__LINE__<<" line."<<std::endl;
                     #endif
                     // if (height != services.getImageProperty(CV_CAP_PROP_FRAME_HEIGHT)) {
                     //     services.setImageProperty(CV_CAP_PROP_FRAME_HEIGHT,height);
                     // }
                     // if (width != services.getImageProperty(CV_CAP_PROP_FRAME_WIDTH)) {
                     //     services.setImageProperty(CV_CAP_PROP_FRAME_WIDTH,width);
                     // }
                     if (brightness != services.getImageProperty(CV_CAP_PROP_BRIGHTNESS)) {
                         services.setImageProperty(CV_CAP_PROP_BRIGHTNESS,brightness);
                     }
                     if (contrast != services.getImageProperty(CV_CAP_PROP_CONTRAST)) {
                         services.setImageProperty(CV_CAP_PROP_CONTRAST,contrast);
                     }
                     if (saturation != services.getImageProperty(CV_CAP_PROP_SATURATION)) {
                         services.setImageProperty(CV_CAP_PROP_SATURATION,saturation);
                     }
                     //set height and width is dangerous
                     //std::cout <<"set image height as : " << height << std::endl;
                     //std::cout <<"set image width as : " << width << std::endl;
                     std::cout <<"set image brightness as : " << brightness << std::endl;
                     std::cout <<"set image contrast as : " << contrast << std::endl;
                     std::cout <<"set image saturation as : " << saturation << std::endl;
                }else if(action == "lightON") {
                     if (!services.lightIsOn()) {
                         services.turnLight(RASPBERRY_ROBOT_LIGHT_STATE_ON);
                         std::cout << "Light On." <<std::endl;
                     }else {
                         std::cout << "Light had been on." <<std::endl;
                     }
                }else if(action == "lightOFF") {
                     if (services.lightIsOn()) {
                         services.turnLight(RASPBERRY_ROBOT_LIGHT_STATE_OFF);
                         std::cout << "Light Off." <<std::endl;
                     }else {
                         std::cout << "Light had been off." <<std::endl;
                     }
                }else if (action == "startDetection") {
                     if (!services.detectorIsStarted()) {
                         services.startDetector();
                         std::cout <<"face detection start." << std::endl;
                     }else {
                         std::cout <<"face detection had been started. "<<std::endl;
                     }
                }else if(action == "stopDetection") {
                     services.stopDetector();
                     std::cout <<"face detection had been stoped." << std::endl;
                }

                //if error detected
                if (error != "")
                {
                     std::cout << error << std::endl;
                     that->loginState = false;
                     //close(that->sockfd);
                     that->reconnection();
                }
                //just for deubg
                 #ifdef DEBUG
                 std::cout<<"ResponseJson: "<<ResponseJson<<std::endl;
                 std::cout <<"In "<<__FILE__<<" , at "<<__LINE__<<" line."<<std::endl;
                 #endif
            }
            else {
                 std::cout << "ResponseJson error Receive!" << std::endl;
            }

            delete[] headBuffer;
            headBuffer = nullptr;
            delete[] dataBuffer;
            dataBuffer = nullptr;
        }//end of while

    }

    void TCPComponent::sendMessage(const void *data, int length) {
         this->sendMutex.lock();
         int writeState = -1;
         int closeState = -1;

         if(this->sockfd <0 || data == nullptr || length < 0) {
            std::cerr <<"write params error."<<std::endl;
            return;
         }

         writeState = write(this->sockfd,data,length);
         //deal with write error
         if(writeState<=0)
         {
            this->loginState = false;

            if(errno == EINTR) {
                 std::cerr <<"server socket write error."<<std::endl;
                 this->reconnection();
            }
            else if(errno == EPIPE) {
                 std::cerr <<"server socket had been closed. And try to connect again after 10 seconds"<<std::endl;
                 this->reconnection();
            }
         }
         this->sendMutex.unlock();
    }//end of sendMessage

    void TCPComponent::sendRequest(const char* JSONBytes, int length){
         char *sendBuffer = new char[5+length];
         sendBuffer[0] = 'r';
         char* ptr = (char*)&length;
         sendBuffer[1] = *ptr;
         sendBuffer[2] = *(ptr+1);
         sendBuffer[3] = *(ptr+2);
         sendBuffer[4] = *(ptr+3);

         for(int i=5;i<5+length;i++){
             sendBuffer[i] = JSONBytes[i-5];
         }
         sendMessage(sendBuffer,5+length);

         delete[] sendBuffer;
         sendBuffer = nullptr;
    }//end of sendRequest


}
