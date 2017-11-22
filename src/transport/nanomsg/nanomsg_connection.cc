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

#include <cassert>
#include <memory>
#include <functional>
#include <unistd.h>

#include "nanomsg_connection.h"
#include "zmap/logger.h"
#include "macros.h"

namespace zdb {

// helper for parsing messages
NanomsgEnvelope* parse_msg(char* msgTxt) {
  // separate header from body
  std::string txt(msgTxt);
  size_t i = txt.find('\n');
  if (std::string::npos == i) {
    log_warn("zdb::parse_msg", "received improperly formatted message - missing endline between header and body");
    return NULL;
  };

  // unpack header
  // e.g.: {"topic":"ipv4", "status": "OK", "error": ""}
  std::string headerTxt = txt.substr(0, i+1);
  Json::Value header;
  Json::Reader reader;
  reader.parse(headerTxt, header);

  auto env = new NanomsgEnvelope();
  env->topic = header.get("topic", "").asString();
  env->status = header.get("status", "ERROR").asString();
  env->error = header.get("error", "").asString();
  env->body = txt.substr(i+1);

  // get the topic, throw the message away if it's not present
  if ("unknown" == env->topic) {
    log_warn("zdb::parse_msg", "Message with header %s has no topic, throwing it away.", headerTxt.c_str());
    delete env;
    return NULL;
  }

  return env;
}

std::string serialize_msg(NanomsgEnvelope* msg) {
  // build json header
  Json::Value root;
  root["topic"] = msg->topic;
  root["status"] = msg->status;
  root["error"] = msg->error;

  // output header
  Json::StreamWriterBuilder wbuilder;
  wbuilder["indentation"] = "";
  std::string header = Json::writeString(wbuilder, root);

  // add body and return
  std::string msgTxt = header + '\n' + msg->body;
  return msgTxt;
}

// ============================================================================
// Class: NanomsgInbound
// Note: This is meant to be a singleton class - it keeps track of a pool of connections, one for each port.
//      You can add and remove subscriptions and the connections will be managed for you.
// ============================================================================

// static
std::unordered_map<std::string, NanomsgInbound*> NanomsgInbound::connections;

void NanomsgInbound::subscribe(const std::string& port, const std::string& topic, const std::string& address) {
  // does a connection for this port exist?
  log_trace("NanomsgInbound", "subscribe called for %s %s -> %s", port.c_str(), topic.c_str(), address.c_str());
  auto conn = connections[port];
  if (!conn) {
    conn = new NanomsgInbound(port);
    connections[port] = conn;
  }
  conn->subscribe(topic, address);
}

void NanomsgInbound::unsubscribe(const std::string& port, const std::string& topic) {
  NanomsgInbound* conn = connections[port];
  if (conn) {
    conn->unsubscribe(topic);
    if (conn->count_subscriptions() < 1) {
      delete conn;
    }
  }
}

// instance
NanomsgInbound::NanomsgInbound(const std::string& port): m_port(port) {
  init_socket();
}

NanomsgInbound::~NanomsgInbound() {
  if (receiver_thread.joinable()) receiver_thread.join();
  nn_shutdown(inbound_socket, 0);
}

void NanomsgInbound::subscribe(const std::string& topic, const std::string& address) {
  // create a socket and add it to the subscriptions
  if (!subscriptions[topic]) {
    auto socket = nn_socket (AF_SP, NN_PUSH);
    nn_connect(socket, address.c_str());
    subscriptions[topic] = socket;
  }
  if (inbound_socket && !m_listening) {
    m_listening = true;
    receiver_thread = std::thread(&NanomsgInbound::listen_and_publish, this);
  }
}

void NanomsgInbound::unsubscribe(const std::string& topic) {
  if (subscriptions[topic]) {
    subscriptions.erase(topic);
  }
}

void NanomsgInbound::init_socket() {
  inbound_socket = nn_socket(AF_SP, NN_PULL);
  int* timeout = new int(1000);
  nn_setsockopt(inbound_socket, NN_SOL_SOCKET, NN_RCVTIMEO, timeout, sizeof(*timeout));

  // is the socket valid?
  if (inbound_socket < 0) {
    log_fatal("NanomsgInbound", "unable to create inbound_socket");
  };
  log_info("NanomsgInbound", "binding port %s", m_port.c_str());
  if (nn_bind(inbound_socket, m_port.c_str()) < 0) {
    log_fatal("NanomsgInbound", "unable to bind inbound_socket to %s, reason: %s", m_port.c_str(), nn_strerror(nn_errno()));
  };
}

void NanomsgInbound::listen_and_publish() {
  log_info("NanomsgInbound", "listen_and_publish(), count: %i", count_subscriptions());
  while(count_subscriptions() > 0) {
    // get socket message
    // translate response or error to Message and return it
    char *buf = NULL;
    int recv_bytes = nn_recv (inbound_socket, &buf, NN_MSG, 0);
    log_info("NanomsgInbound", "received:\n%s", buf);

    // warn on empty message
    if (recv_bytes < 0) {
      log_info("NanomsgInbound", "connection received empty message, reason: %s", nn_strerror(nn_errno()));
      continue;
    };

    NanomsgEnvelope* env = parse_msg(buf);
    nn_freemsg(buf);
    if (!env) {
      log_warn("NanomsgInbound", "Couldn't parse message.");
      continue;
    }

    while (1) {
      // if receiver exists, deliver the message
      auto socket = subscriptions[env->topic];
      if (socket) {
        log_trace("NanomsgInbound", "forwarding message to topic: %s", env->topic.c_str());
        int sent_bytes = nn_send(socket, buf, recv_bytes, 0);
        if (sent_bytes < 0) {
          log_warn("NanomsgInbound", "connection forwarded empty message, reason: %s", nn_strerror(nn_errno()));
        }
        break;
      }

      // otherwise complain and sleep
      log_info("NanomsgInbound", "no subscribers for topic %s, waiting...", env->topic.c_str());
      sleep(1);
    }
  }
  m_listening = false;
}

// ============================================================================
// Class: NanomsgOutbound
// ============================================================================

// static
std::unordered_map<std::string, NanomsgOutbound*> NanomsgOutbound::connections;

void NanomsgOutbound::bind(const std::string& publish_address) {
  if (connections[publish_address] != NULL) {
    return;
  }
  connections[publish_address] = new NanomsgOutbound(publish_address);
}

int NanomsgOutbound::publish(const std::string& publish_address, const std::string& msg) {
  if (connections[publish_address] == NULL) {
    log_warn("NanomsgOutbound", "published to a non-existent address: %s", publish_address.c_str());
    return 0;
  }
  return connections[publish_address]->publish(msg);
}

// instance
NanomsgOutbound::NanomsgOutbound(const std::string& publish_address): m_publish_address(publish_address) {
  init_socket();
}

NanomsgOutbound::~NanomsgOutbound() {
  if (publish_thread.joinable()) publish_thread.join();
  nn_shutdown(publish_socket, 0);
}

void NanomsgOutbound::init_socket() {
  // set up publish_socket
  publish_socket = nn_socket(AF_SP, NN_PUB);
  log_info("NanomsgOutbound", "binding port %s", m_publish_address.c_str());
  if (nn_bind(publish_socket, m_publish_address.c_str()) < 0) {
    log_fatal("NanomsgOutbound", "unable to bind inbound_socket to %s, reason: %s", m_publish_address.c_str(), nn_strerror(nn_errno()));
  };
}

int NanomsgOutbound::publish(const std::string& msgTxt) {
  int sent_bytes = nn_send(publish_socket, msgTxt.c_str(), msgTxt.length() + 1, 0);
  if (sent_bytes < 0) {
    log_warn("NanomsgOutbound", "delta sent empty message");
  }
  return sent_bytes;
}

// ============================================================================
// Class: NanomsgConsumerConnection
// ============================================================================

NanomsgConsumerConnection::NanomsgConsumerConnection(): ConsumerConnection() {}
NanomsgConsumerConnection::~NanomsgConsumerConnection() {
  NanomsgInbound::unsubscribe(m_port, m_topic_name);
}

bool NanomsgConsumerConnection::connect(const Json::Value& nanomsg_config,
                              const std::string& topic,
                              const std::string& client_id) {
  if (m_connected) {
    return true;
  }
  m_port = nanomsg_config.get("consumer_port", "127.0.0.1:4055").asString();
  m_topic_name = topic;

  // set up inbound_socket
  std::string address = "inproc://" + m_topic_name;
  inbound_socket = nn_socket(AF_SP, NN_PULL);
  int* timeout = new int(1000);
  nn_setsockopt(inbound_socket, NN_SOL_SOCKET, NN_RCVTIMEO, timeout, sizeof(*timeout));
  if (nn_bind(inbound_socket, address.c_str()) < 0) {
    log_fatal("NanomsgConsumerConnection", "unable to bind inbound_socket to %s, reason: %s", address.c_str(), nn_strerror(nn_errno()));
  };

  // subscribe our topic to inbound_socket
  NanomsgInbound::subscribe(m_port, m_topic_name, address);

  m_connected = true;
  return true;
}

Message NanomsgConsumerConnection::consume() {
  char *buf = NULL;
  int recv_bytes = nn_recv(inbound_socket, &buf, NN_MSG, 0);

  // if message is empty assume we timed out
  if (recv_bytes < 0) {
    Message msg;
    msg.status = Message::Status::WOULD_BLOCK;
    msg.error = nn_strerror(nn_errno());
    return msg;
  };

  auto nn_msg = parse_msg(buf);

  if ("error" == nn_msg->status) {
    Message msg;
    msg.status = Message::Status::ERROR;
    msg.error = nn_msg->error;
    return msg;
  } else {
    Message msg;
    msg.status = Message::Status::OK;
    msg.data = nn_msg->body;
    return msg;
  }

  delete(nn_msg);
  nn_freemsg(buf);
}

// ============================================================================
// Class: NanomsgProducerConnection
// ============================================================================

NanomsgProducerConnection::NanomsgProducerConnection(): ProducerConnection() {};
NanomsgProducerConnection::~NanomsgProducerConnection() {
  //NanomsgOutbound::unbind(m_port);  // something to think about?
}

bool NanomsgProducerConnection::connect(const Json::Value& nanomsg_config,
                              const std::string& topic,
                              const std::string& client_id) {
  if (m_connected) {
    return true;
  }
  m_port = nanomsg_config.get("producer_port", "tcp://127.0.0.1:4056").asString();
  m_topic_name = topic;

  NanomsgOutbound::bind(m_port);

  m_connected = true;
  return m_connected;
}

Message NanomsgProducerConnection::produce(const std::string& msg) {

  // put the message in an envelope
  NanomsgEnvelope env;
  env.topic = m_topic_name;
  env.status = "OK";
  env.body = msg;

  // serialize the message/envelope
  std::string msgTxt = serialize_msg(&env);

  // send it
  NanomsgOutbound::publish(m_port, msgTxt);

  Message ret;
  ret.status = Message::Status::OK;
  return ret;
}

// Nanomsg always blocks
Message NanomsgProducerConnection::produce_blocking(const std::string& msg, size_t max_attempts) {
  return produce(msg);
}


}  // namespace zdb
