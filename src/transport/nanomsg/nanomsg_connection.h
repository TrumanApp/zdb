/*
 * ZDB Copyright 2017 Regents of the University of Michigan
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#ifndef ZDB_SRC_NANOMSG_TOPIC_H
#define ZDB_SRC_NANOMSG_TOPIC_H

#include <string>
#include <thread>
#include <unordered_map>
#include <functional>
#include <nanomsg/nn.h>
#include <nanomsg/pipeline.h>
#include <nanomsg/pubsub.h>
#include <json/json.h>

#include "transport/connection.h"
#include "zmap/logger.h"

namespace zdb {

struct NanomsgEnvelope {
  std::string status;
  std::string topic;
  std::string error;
  std::string body;
};

NanomsgEnvelope* parse_msg(char* msgTxt);
std::string serialize_msg(NanomsgEnvelope* msg);

class NanomsgInbound {
  public:
    // 1) create a connection for this port if one does not already exist
    // 2) add the topic/handler pair to the subscribers list
    static void subscribe(const std::string& port, const std::string& topic, const std::string& address);

    // 1) remove the topic/handler pair from the subscribers list
    // 2) if there are no remaining handlers, destroy the connection
    static void unsubscribe(const std::string& port, const std::string& topic);

    // you can destroy 'em but you can't build 'em
    ~NanomsgInbound();

  private:
    // keep track of connections
    static std::unordered_map<std::string, NanomsgInbound*> connections;

    // don't use the class methods directly, just use the static subscribe/unsubscribe
    NanomsgInbound(const std::string& port);
    void init_socket();
    void listen_and_publish();
    int count_subscriptions() {return subscriptions.size();};
    void subscribe(const std::string& topic, const std::string& address);
    void unsubscribe(const std::string& topic);

    // started on constructor, joined on destructor
    std::thread receiver_thread;
    std::string m_port;
    int inbound_socket;
    std::unordered_map<std::string, int> subscriptions;
};

class NanomsgOutbound {
  public:
    // bind a socket to the port if we haven't already
    static void bind(const std::string& publish_address);

    // push the message out
    static int publish(const std::string& publish_address, const std::string& msgTxt);

    ~NanomsgOutbound();
  private:
    // keep track of connections
    static std::unordered_map<std::string, NanomsgOutbound*> connections;

    NanomsgOutbound(const std::string& publish_address);
    void init_socket();
    int publish(const std::string& msgTxt);
    std::thread publish_thread;
    std::string m_publish_address;
    int publish_socket;
};

class NanomsgConsumerConnection : virtual public ConsumerConnection {
 public:
  NanomsgConsumerConnection();
  ~NanomsgConsumerConnection();

  ClientType client_type() const override {
    return ClientType::NANOMSG_CONSUMER;
  }

  bool connect(const Json::Value& nanomsg_config,
               const std::string& topic,
               const std::string& client_id) override;

  Message consume() override;

 private:
  std::string m_port;
  std::string m_topic;
  int inbound_socket;
};

class NanomsgProducerConnection : virtual public ProducerConnection {
 public:
  NanomsgProducerConnection();
  ~NanomsgProducerConnection();

  ClientType client_type() const override {
    return ClientType::NANOMSG_PRODUCER;
  }

  bool connect(const Json::Value& nanomsg_config,
               const std::string& topic,
               const std::string& client_id) override;

  Message produce(const std::string& msg) override;
  Message produce_blocking(const std::string& msg, size_t max_attempts) override;

 private:
  std::string m_port;
  std::string m_topic;
};

}  // namespace zdb

#endif /* ZDB_SRC_NANOMSG_TOPIC_H */
